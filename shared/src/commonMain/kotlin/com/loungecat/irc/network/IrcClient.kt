package com.loungecat.irc.network

import com.loungecat.irc.data.model.*
import com.loungecat.irc.util.IrcCommand
import com.loungecat.irc.util.IrcCommandParser
import com.loungecat.irc.util.Logger
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import net.engio.mbassy.listener.Handler
import org.kitteh.irc.client.library.Client
import org.kitteh.irc.client.library.element.Channel as KittehChannel
import org.kitteh.irc.client.library.element.User
import org.kitteh.irc.client.library.event.channel.*
import org.kitteh.irc.client.library.event.client.ClientReceiveCommandEvent
import org.kitteh.irc.client.library.event.client.ClientReceiveNumericEvent
import org.kitteh.irc.client.library.event.connection.ClientConnectionEndedEvent
import org.kitteh.irc.client.library.event.connection.ClientConnectionEstablishedEvent
import org.kitteh.irc.client.library.event.user.PrivateMessageEvent
import org.kitteh.irc.client.library.event.user.UserNickChangeEvent
import org.kitteh.irc.client.library.event.user.UserQuitEvent

class IrcClient(
        override var config: ServerConfig,
        override val serverId: Long,
        private val scope: CoroutineScope
) : ChatClient {
    private var client: Client? = null
    private var eventListener: EventListener? = null

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Connecting)
    override val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _messages = MutableSharedFlow<IncomingMessage>()
    override val messages: SharedFlow<IncomingMessage> = _messages.asSharedFlow()

    private val _channels = MutableStateFlow<Map<String, Channel>>(emptyMap())
    override val channels: StateFlow<Map<String, Channel>> = _channels.asStateFlow()

    // WHOIS cache: nickname (lowercase) -> formatted WHOIS info
    private val _whoisCache = MutableStateFlow<Map<String, String>>(emptyMap())
    override val whoisCache: StateFlow<Map<String, String>> = _whoisCache.asStateFlow()

    // Pending WHOIS: nickname (lowercase) -> list of info lines being collected
    private val pendingWhois = mutableMapOf<String, MutableList<String>>()
    private val silentWhoisTargets = mutableSetOf<String>()

    override fun requestSilentWhois(nickname: String) {
        val nick = nickname.lowercase()
        silentWhoisTargets.add(nick)
        scope.launch { sendRawCommand("WHOIS $nickname") }
    }

    private val previousTopics = mutableMapOf<String, String?>()
    private var hasConnectedBefore = false
    private val initialJoinChannels = mutableSetOf<String>()

    override suspend fun connect() {
        try {
            // Clean up any existing client before creating a new one (important for reconnects)
            eventListener?.let { listener ->
                try {
                    client?.eventManager?.unregisterEventListener(listener)
                } catch (e: Exception) {
                    Logger.e("IrcClient", "Error unregistering old event listener", e)
                }
            }
            eventListener = null

            client?.let { oldClient ->
                try {
                    oldClient.shutdown("Reconnecting")
                } catch (e: Exception) {
                    Logger.e("IrcClient", "Error shutting down old client", e)
                }
            }
            client = null

            Logger.d("IrcClient", "=== CONNECTION ATTEMPT START ===")
            Logger.d("IrcClient", "Server: ${config.hostname}:${config.port}")
            Logger.d("IrcClient", "SSL: ${config.useSsl}, Nickname: ${config.nickname}")

            _connectionState.value = ConnectionState.Connecting

            val clientBuilder =
                    Client.builder()
                            .nick(config.nickname)
                            .user(config.username.ifEmpty { config.nickname })
                            .realName(config.realName.ifEmpty { config.nickname })
                            .server()
                            .host(config.hostname)
                            .port(
                                    config.port,
                                    if (config.useSsl) Client.Builder.Server.SecurityType.SECURE
                                    else Client.Builder.Server.SecurityType.INSECURE
                            )
                            .apply {
                                if (!config.serverPassword.isNullOrEmpty()) {
                                    password(config.serverPassword)
                                }
                            }
                            .then()

            client = clientBuilder.build()

            // Proxy Injection (Reflection)
            if (config.proxyType != ProxyType.NONE && config.proxyHost.isNotBlank()) {
                try {
                    Logger.d("IrcClient", "Attempting to inject proxy via reflection...")

                    val clientImpl = client!!

                    // Search for Bootstrap field in client implementation
                    var bootstrapField: java.lang.reflect.Field? = null
                    var currentClass: Class<*>? = clientImpl.javaClass

                    while (currentClass != null) {
                        try {
                            for (field in currentClass.declaredFields) {
                                // Check for Bootstrap field (by type name to find it, but we need
                                // the object)
                                if (field.type.name.endsWith("Bootstrap")) {
                                    bootstrapField = field
                                    break
                                }
                            }
                        } catch (e: Exception) {
                            Logger.w("IrcClient", "Failed to inspect fields in ${currentClass.name} during proxy injection")
                        }
                        if (bootstrapField != null) break
                        currentClass = currentClass.superclass
                    }

                    if (bootstrapField != null) {
                        bootstrapField.isAccessible = true
                        val bootstrap =
                                bootstrapField.get(clientImpl) as? io.netty.bootstrap.Bootstrap

                        if (bootstrap != null) {
                            val originalHandler = bootstrap.config().handler()

                            bootstrap.handler(
                                    object :
                                            io.netty.channel.ChannelInitializer<
                                                    io.netty.channel.socket.SocketChannel>() {
                                        override fun initChannel(
                                                ch: io.netty.channel.socket.SocketChannel
                                        ) {
                                            val pipeline = ch.pipeline()

                                            val proxyAddress =
                                                    java.net.InetSocketAddress(
                                                            config.proxyHost,
                                                            config.proxyPort
                                                    )

                                            Logger.d(
                                                    "IrcClient",
                                                    "Injecting ${config.proxyType} proxy handler for $proxyAddress"
                                            )

                                            if (config.proxyType == ProxyType.HTTP) {
                                                val user = config.proxyUsername
                                                val pass = config.proxyPassword
                                                if (!user.isNullOrEmpty() && !pass.isNullOrEmpty()
                                                ) {
                                                    pipeline.addFirst(
                                                            "proxy",
                                                            io.netty.handler.proxy.HttpProxyHandler(
                                                                    proxyAddress,
                                                                    user,
                                                                    pass
                                                            )
                                                    )
                                                } else {
                                                    pipeline.addFirst(
                                                            "proxy",
                                                            io.netty.handler.proxy.HttpProxyHandler(
                                                                    proxyAddress
                                                            )
                                                    )
                                                }
                                            } else if (config.proxyType == ProxyType.SOCKS) {
                                                val user = config.proxyUsername
                                                val pass = config.proxyPassword
                                                if (!user.isNullOrEmpty() && !pass.isNullOrEmpty()
                                                ) {
                                                    pipeline.addFirst(
                                                            "proxy",
                                                            io.netty.handler.proxy
                                                                    .Socks5ProxyHandler(
                                                                            proxyAddress,
                                                                            user,
                                                                            pass
                                                                    )
                                                    )
                                                } else {
                                                    pipeline.addFirst(
                                                            "proxy",
                                                            io.netty.handler.proxy
                                                                    .Socks5ProxyHandler(
                                                                            proxyAddress
                                                                    )
                                                    )
                                                }
                                            }

                                            // Chain original handler
                                            if (originalHandler != null) {
                                                pipeline.addLast(originalHandler)
                                            }
                                        }
                                    }
                            )
                            Logger.d(
                                    "IrcClient",
                                    "Proxy injected successfully into Netty Bootstrap"
                            )
                        } else {
                            Logger.e(
                                    "IrcClient",
                                    "Bootstrap field found but valid instance was null or not standard Bootstrap"
                            )
                        }
                    } else {
                        Logger.e(
                                "IrcClient",
                                "Could not find Bootstrap field in Client class: ${clientImpl.javaClass.name}"
                        )
                    }
                } catch (e: Exception) {
                    Logger.e("IrcClient", "Failed to inject proxy via reflection", e)
                }
            }

            if (config.useSasl &&
                            !config.saslUsername.isNullOrEmpty() &&
                            !config.saslPassword.isNullOrEmpty()
            ) {
                Logger.d("IrcClient", "Configuring SASL PLAIN authentication")
                try {
                    client?.authManager?.addProtocol(
                            org.kitteh.irc.client.library.feature.auth.SaslPlain(
                                    client!!,
                                    config.saslUsername!!,
                                    config.saslPassword!!
                            )
                    )
                } catch (e: Exception) {
                    Logger.e("IrcClient", "Failed to configure SASL", e)
                }
            }

            eventListener = EventListener()
            client?.eventManager?.registerEventListener(eventListener!!)
            client?.connect()
            Logger.d("IrcClient", "Connect call completed")
        } catch (e: Exception) {
            Logger.e("IrcClient", "=== CONNECTION FAILED ===", e)
            _connectionState.value = ConnectionState.Error(e.message ?: "Connection failed")
            throw e
        }
    }

    override fun updateConfig(newConfig: ServerConfig) {
        this.config = newConfig
    }

    override suspend fun disconnect() {
        // Unregister event listener before shutdown to prevent ghost events
        eventListener?.let { listener -> client?.eventManager?.unregisterEventListener(listener) }
        eventListener = null
        client?.shutdown("Disconnecting")
        client = null
        _connectionState.value = ConnectionState.Disconnected
    }

    override suspend fun sendMessage(target: String, message: String) {
        client?.sendMessage(target, message)

        val isAction = message.startsWith("\u0001ACTION") && message.endsWith("\u0001")
        val content =
                if (isAction) {
                    message.removePrefix("\u0001ACTION ").removeSuffix("\u0001")
                } else {
                    message
                }
        val type = if (isAction) MessageType.ACTION else MessageType.NORMAL

        _messages.emit(
                IncomingMessage(
                        target = target,
                        sender = config.nickname,
                        content = content,
                        type = type,
                        isSelf = true
                )
        )
    }

    override suspend fun joinChannel(channelName: String) {
        client?.addChannel(channelName)
    }

    override suspend fun partChannel(channelName: String) {
        client?.removeChannel(channelName)
    }

    override suspend fun sendRawCommand(command: String) {
        client?.sendRawLine(command)
    }

    override suspend fun closeQuery(nickname: String) {
        _channels.update { channels -> channels - nickname }
    }

    override suspend fun startQuery(nickname: String) {
        _channels.update { channels ->
            if (!channels.containsKey(nickname)) {
                channels + (nickname to Channel(name = nickname, type = ChannelType.QUERY))
            } else {
                channels
            }
        }
    }

    override fun getCurrentNickname(): String = client?.nick ?: config.nickname

    inner class EventListener {

        @Handler
        fun onConnectionEstablished(event: ClientConnectionEstablishedEvent) {
            Logger.d("IrcClient", ">>> EVENT: ClientConnectionEstablishedEvent RECEIVED")
            try {
                scope.launch {
                    _connectionState.value = ConnectionState.Connected(config.serverName)

                    // Start Keep-Alive Pinger
                    launch {
                        while (isActive) {
                            delay(30000) // 30 seconds
                            try {
                                client?.sendRawLine("PING :keepalive")
                            } catch (e: Exception) {
                                Logger.e("IrcClient", "Failed to send keepalive PING", e)
                            }
                        }
                    }

                    val serverChannelName = "* ${config.serverName}"
                    _channels.update { channels ->
                        channels +
                                (serverChannelName to
                                        Channel(
                                                name = serverChannelName,
                                                type = ChannelType.SERVER
                                        ))
                    }

                    if (!hasConnectedBefore) {
                        _messages.emit(
                                IncomingMessage(
                                        target = serverChannelName,
                                        sender = "Server",
                                        content =
                                                "Connected to ${config.serverName}. Waiting for registration...",
                                        type = MessageType.SERVER
                                )
                        )
                    } else {
                        _messages.emit(
                                IncomingMessage(
                                        target = serverChannelName,
                                        sender = "Server",
                                        content =
                                                "Reconnected to ${config.serverName}. Waiting for registration...",
                                        type = MessageType.SERVER
                                )
                        )
                    }
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onConnectionEstablished", e)
            }
        }

        @Handler
        fun onConnectionEnded(event: ClientConnectionEndedEvent) {
            Logger.d("IrcClient", ">>> EVENT: ClientConnectionEndedEvent RECEIVED")
            try {
                scope.launch {
                    _connectionState.value = ConnectionState.Disconnected

                    val serverChannelName = "* ${config.serverName}"
                    val disconnectReason =
                            if (event.cause.isPresent) ": ${event.cause.get()}" else ""

                    _messages.emit(
                            IncomingMessage(
                                    target = serverChannelName,
                                    sender = "Server",
                                    content = "Disconnected from server$disconnectReason",
                                    type = MessageType.SERVER
                            )
                    )
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onConnectionEnded", e)
            }
        }

        @Handler
        fun onChannelJoin(event: ChannelJoinEvent) {
            try {
                scope.launch {
                    val channelName = event.channel.name
                    Logger.d(
                            "IrcClient",
                            "DEBUG: onChannelJoin event for $channelName (User: ${event.user.nick})"
                    )

                    if (event.user.nick == client?.nick) {
                        initialJoinChannels.add(channelName)
                        _channels.update { channels ->
                            channels +
                                    (channelName to
                                            Channel(name = channelName, type = ChannelType.CHANNEL))
                        }
                        // Update user list immediately - Kitteh should have users from NAMES
                        updateChannelUsers(event.channel)
                    } else {
                        if (!initialJoinChannels.contains(channelName)) {
                            _messages.emit(
                                    IncomingMessage(
                                            target = channelName,
                                            sender = event.user.nick,
                                            content = "has joined",
                                            type = MessageType.JOIN
                                    )
                            )
                        }
                        updateChannelUsers(event.channel)
                    }
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onChannelJoin", e)
            }
        }

        @Handler
        fun onChannelPart(event: ChannelPartEvent) {
            try {
                scope.launch {
                    val channelName = event.channel.name

                    if (event.user.nick == client?.nick) {
                        _channels.update { channels -> channels - channelName }
                    } else {
                        _messages.emit(
                                IncomingMessage(
                                        target = channelName,
                                        sender = event.user.nick,
                                        content = event.message,
                                        type = MessageType.PART
                                )
                        )
                        updateChannelUsers(event.channel)
                    }
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onChannelPart", e)
            }
        }

        @Handler
        fun onChannelMessage(event: ChannelMessageEvent) {
            try {
                scope.launch {
                    val message = event.message
                    val isAction = message.startsWith("\u0001ACTION") && message.endsWith("\u0001")

                    val content =
                            if (isAction) {
                                message.removePrefix("\u0001ACTION ").removeSuffix("\u0001")
                            } else {
                                message
                            }

                    _messages.emit(
                            IncomingMessage(
                                    target = event.channel.name,
                                    sender = event.actor.nick,
                                    content = content,
                                    type = if (isAction) MessageType.ACTION else MessageType.NORMAL,
                                    isSelf = false
                            )
                    )
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onChannelMessage", e)
            }
        }

        @Handler
        fun onPrivateMessage(event: PrivateMessageEvent) {
            try {
                scope.launch {
                    val sender = event.actor.nick

                    _channels.update { channels ->
                        if (!channels.containsKey(sender)) {
                            channels + (sender to Channel(name = sender, type = ChannelType.QUERY))
                        } else {
                            channels
                        }
                    }

                    _messages.emit(
                            IncomingMessage(
                                    target = sender,
                                    sender = sender,
                                    content = event.message,
                                    type = MessageType.NORMAL,
                                    isSelf = false
                            )
                    )
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onPrivateMessage", e)
            }
        }

        @Handler
        fun onChannelTopic(event: ChannelTopicEvent) {
            try {
                scope.launch {
                    val channelName = event.channel.name
                    val newTopic = event.newTopic
                    val topic = newTopic.value.orElse(null)

                    val previousTopic = previousTopics[channelName]
                    val topicChanged = previousTopic != topic
                    previousTopics[channelName] = topic

                    _channels.update { channels ->
                        val channel = channels[channelName]
                        if (channel != null) {
                            channels + (channelName to channel.copy(topic = topic))
                        } else {
                            channels
                        }
                    }

                    if (topicChanged) {
                        val setter =
                                if (newTopic.setter.isPresent) {
                                    val setterActor = newTopic.setter.get()
                                    if (setterActor is User) setterActor.nick else "Server"
                                } else {
                                    "Server"
                                }

                        _messages.emit(
                                IncomingMessage(
                                        target = channelName,
                                        sender = setter,
                                        content = "Topic: ${topic ?: "(no topic)"}",
                                        type = MessageType.TOPIC
                                )
                        )
                    }
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onChannelTopic", e)
            }
        }

        @Handler
        fun onChannelUsersUpdated(event: ChannelUsersUpdatedEvent) {
            try {
                scope.launch {
                    val channelName = event.channel.name
                    Logger.d(
                            "IrcClient",
                            ">>> ChannelUsersUpdatedEvent for $channelName: ${event.channel.users.size} users"
                    )
                    updateChannelUsers(event.channel)
                    initialJoinChannels.remove(channelName)
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onChannelUsersUpdated", e)
            }
        }

        @Handler
        fun onChannelNamesUpdated(event: ChannelNamesUpdatedEvent) {
            try {
                scope.launch {
                    val channelName = event.channel.name
                    Logger.d(
                            "IrcClient",
                            ">>> ChannelNamesUpdatedEvent for $channelName: ${event.channel.users.size} users. Names: ${event.channel.users.joinToString { it.nick }}"
                    )
                    updateChannelUsers(event.channel)
                    initialJoinChannels.remove(channelName)
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onChannelNamesUpdated", e)
            }
        }

        @Handler
        fun onChannelKick(event: ChannelKickEvent) {
            try {
                scope.launch {
                    _messages.emit(
                            IncomingMessage(
                                    target = event.channel.name,
                                    sender = event.actor.name,
                                    content = "${event.target.nick} was kicked: ${event.message}",
                                    type = MessageType.KICK
                            )
                    )

                    if (event.target.nick == client?.nick) {
                        _channels.update { it - event.channel.name }
                    }
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onChannelKick", e)
            }
        }

        @Handler
        fun onChannelNotice(event: ChannelNoticeEvent) {
            try {
                scope.launch {
                    _messages.emit(
                            IncomingMessage(
                                    target = event.channel.name,
                                    sender = event.actor.nick,
                                    content = event.message,
                                    type = MessageType.NOTICE
                            )
                    )
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onChannelNotice", e)
            }
        }

        @Handler
        fun onServerNotice(event: ClientReceiveCommandEvent) {
            if (event.command == "NOTICE") {
                try {
                    scope.launch {
                        val target = event.parameters.getOrNull(0) ?: ""
                        val message = event.parameters.drop(1).joinToString(" ")
                        val sender = event.actor.name
                        val serverChannelName = "* ${config.serverName}"

                        // Determine where to show the notice
                        // If it's from a service (NickServ, etc) OR addressed to us privately, show
                        // in server tab UNLESS we have a query window open for them.

                        val destination =
                                if (channels.value.containsKey(sender)) {
                                    sender
                                } else {
                                    serverChannelName
                                }

                        _messages.emit(
                                IncomingMessage(
                                        target = destination,
                                        sender = sender,
                                        content = message,
                                        type = MessageType.NOTICE
                                )
                        )
                    }
                } catch (e: Exception) {
                    Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onServerNotice", e)
                }
            }
        }

        @Handler
        fun onUserNickChange(event: UserNickChangeEvent) {
            try {
                scope.launch {
                    val oldNick = event.oldUser.nick
                    val newNick = event.newUser.nick

                    // Update user lists in all channels this user is in
                    _channels.update { channels ->
                        channels.mapValues { (_, channel) ->
                            if (channel.users.any { it.nickname == oldNick }) {
                                val updatedUsers =
                                        channel.users.map { user ->
                                            if (user.nickname == oldNick) {
                                                user.copy(nickname = newNick)
                                            } else {
                                                user
                                            }
                                        }
                                channel.copy(users = updatedUsers)
                            } else {
                                channel
                            }
                        }
                    }

                    // Emit NICK message to all channels where user was present
                    _channels.value.forEach { (channelName, channel) ->
                        if (channel.type == ChannelType.CHANNEL &&
                                        channel.users.any { it.nickname == newNick }
                        ) {
                            _messages.emit(
                                    IncomingMessage(
                                            target = channelName,
                                            sender = oldNick,
                                            content = "is now known as $newNick",
                                            type = MessageType.NICK
                                    )
                            )
                        }
                    }
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onUserNickChange", e)
            }
        }

        @Handler
        fun onUserQuit(event: UserQuitEvent) {
            try {
                scope.launch {
                    val quittingNick = event.user.nick
                    val quitMessage = event.message

                    // Find all channels where this user was present and emit quit message
                    _channels.value.forEach { (channelName, channel) ->
                        if (channel.type == ChannelType.CHANNEL &&
                                        channel.users.any { it.nickname == quittingNick }
                        ) {
                            _messages.emit(
                                    IncomingMessage(
                                            target = channelName,
                                            sender = quittingNick,
                                            content = quitMessage,
                                            type = MessageType.QUIT
                                    )
                            )
                        }
                    }

                    // Remove user from all channel user lists
                    _channels.update { channels ->
                        channels.mapValues { (_, channel) ->
                            if (channel.users.any { it.nickname == quittingNick }) {
                                channel.copy(
                                        users = channel.users.filter { it.nickname != quittingNick }
                                )
                            } else {
                                channel
                            }
                        }
                    }
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onUserQuit", e)
            }
        }

        @Handler
        fun onChannelMode(event: ChannelModeEvent) {
            try {
                scope.launch {
                    updateChannelUsers(event.channel)
                    Logger.d(
                            "IrcClient",
                            "DEBUG: Channel mode changed for ${event.channel.name}, updating users"
                    )
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onChannelMode", e)
            }
        }

        private fun updateChannelUsers(channel: KittehChannel) {
            scope.launch {
                Logger.d(
                        "IrcClient",
                        "DEBUG: updateChannelUsers called for ${channel.name}. Kitteh reports ${channel.users.size} users."
                )
                val users =
                        channel.users.map { user ->
                            ChannelUser(nickname = user.nick, modes = getUserModes(channel, user))
                        }

                Logger.d(
                        "IrcClient",
                        "DEBUG: Mapped users for ${channel.name}: ${users.joinToString { it.nickname }}"
                )

                _channels.update { channels ->
                    val existingChannel = channels[channel.name]
                    if (existingChannel != null) {
                        Logger.d(
                                "IrcClient",
                                "DEBUG: Found channel ${channel.name} in state. Updating users."
                        )
                        channels + (channel.name to existingChannel.copy(users = users))
                    } else {
                        Logger.d(
                                "IrcClient",
                                "DEBUG: Channel ${channel.name} NOT found in state. Keys: ${channels.keys}"
                        )
                        channels
                    }
                }
            }
        }

        private fun getUserModes(channel: KittehChannel, user: User): Set<UserMode> {
            val modes = mutableSetOf<UserMode>()

            val userModes = channel.getUserModes(user)
            if (userModes.isPresent) {
                userModes.get().forEach { mode ->
                    Logger.d(
                            "IrcClient",
                            "DEBUG: processing mode char '${mode.char}' nickPrefix '${mode.nickPrefix}' for user ${user.nick}"
                    )
                    // Check both mode char (o, v) and nick prefix (@, +) because Kitteh might
                    // return either
                    // depending on the version/server config interpretation
                    when (mode.char) {
                        '~', 'q' -> modes.add(UserMode.OWNER)
                        '&', 'a' -> modes.add(UserMode.ADMIN)
                        '@', 'o' -> modes.add(UserMode.OP)
                        '%', 'h' -> modes.add(UserMode.HALFOP)
                        '+', 'v' -> modes.add(UserMode.VOICE)
                        else -> {
                            // Fallback to checking nick prefix just in case
                            when (mode.nickPrefix) {
                                '~' -> modes.add(UserMode.OWNER)
                                '&' -> modes.add(UserMode.ADMIN)
                                '@' -> modes.add(UserMode.OP)
                                '%' -> modes.add(UserMode.HALFOP)
                                '+' -> modes.add(UserMode.VOICE)
                                else ->
                                        Logger.d(
                                                "IrcClient",
                                                "DEBUG: Unknown mode char '${mode.char}' and prefix '${mode.nickPrefix}'"
                                        )
                            }
                        }
                    }
                }
            } else {
                Logger.d(
                        "IrcClient",
                        "DEBUG: No modes found for user ${user.nick} in ${channel.name}"
                )
            }

            return modes
        }

        @Handler
        fun onNumericEvent(event: ClientReceiveNumericEvent) {
            try {
                scope.launch {
                    val serverChannelName = "* ${config.serverName}"
                    val numeric = event.numeric

                    // Handle WHOIS and other server information numerics
                    when (numeric) {
                        // WHOIS Numerics - collect into pendingWhois map
                        311 -> { // RPL_WHOISUSER: <nick> <user> <host> * :<realname>
                            val params = event.parameters
                            if (params.size >= 6) {
                                val nick = params[1].lowercase()
                                val user = params[2]
                                val host = params[3]
                                val realName = params[5]
                                pendingWhois
                                        .getOrPut(nick) { mutableListOf() }
                                        .add("$user@$host ($realName)")

                                if (!silentWhoisTargets.contains(nick)) {
                                    val isSelf =
                                            nick.equals(getCurrentNickname(), ignoreCase = true)
                                    if (!isSelf) {
                                        startQuery(params[1]) // Ensure query window exists
                                    }

                                    _messages.emit(
                                            IncomingMessage(
                                                    target =
                                                            if (isSelf) serverChannelName
                                                            else params[1], // Use original case
                                                    // nick
                                                    sender = "WHOIS",
                                                    content = "$nick ($user@$host): $realName",
                                                    type = MessageType.SERVER
                                            )
                                    )
                                }
                            }
                        }
                        312 -> { // RPL_WHOISSERVER: <nick> <server> :<serverinfo>
                            val params = event.parameters
                            if (params.size >= 4) {
                                val nick = params[1].lowercase()
                                val server = params[2]
                                pendingWhois[nick]?.add("Server: $server")

                                if (!silentWhoisTargets.contains(nick)) {
                                    val isSelf =
                                            nick.equals(getCurrentNickname(), ignoreCase = true)
                                    _messages.emit(
                                            IncomingMessage(
                                                    target =
                                                            if (isSelf) serverChannelName
                                                            else params[1],
                                                    sender = "WHOIS",
                                                    content =
                                                            "Server: $server (${params.getOrNull(3) ?: ""})",
                                                    type = MessageType.SERVER
                                            )
                                    )
                                }
                            }
                        }
                        319 -> { // RPL_WHOISCHANNELS: <nick> :<channels>
                            val params = event.parameters
                            if (params.size >= 3) {
                                val nick = params[1].lowercase()
                                val channels = params.drop(2).joinToString(" ")
                                pendingWhois[nick]?.add("Channels: $channels")

                                if (!silentWhoisTargets.contains(nick)) {
                                    val isSelf =
                                            nick.equals(getCurrentNickname(), ignoreCase = true)
                                    _messages.emit(
                                            IncomingMessage(
                                                    target =
                                                            if (isSelf) serverChannelName
                                                            else params[1],
                                                    sender = "WHOIS",
                                                    content = "Channels: $channels",
                                                    type = MessageType.SERVER
                                            )
                                    )
                                }
                            }
                        }
                        317 -> { // RPL_WHOISIDLE: <nick> <seconds> <signon> :<info>
                            val params = event.parameters
                            if (params.size >= 3) {
                                val nick = params[1].lowercase()
                                val idleSeconds = params[2].toLongOrNull() ?: 0
                                val idleMinutes = idleSeconds / 60
                                val signon = params.getOrNull(3)?.toLongOrNull() ?: 0

                                val idleMsg =
                                        if (idleMinutes > 0) "Idle: ${idleMinutes}m" else "Idle: 0m"
                                pendingWhois[nick]?.add(idleMsg)

                                if (!silentWhoisTargets.contains(nick)) {
                                    val isSelf =
                                            nick.equals(getCurrentNickname(), ignoreCase = true)
                                    _messages.emit(
                                            IncomingMessage(
                                                    target =
                                                            if (isSelf) serverChannelName
                                                            else params[1],
                                                    sender = "WHOIS",
                                                    content = "$idleMsg (Signon: $signon)",
                                                    type = MessageType.SERVER
                                            )
                                    )
                                }
                            }
                        }
                        330 -> { // RPL_WHOISACCOUNT: <nick> <account> :<info>
                            val params = event.parameters
                            if (params.size >= 3) {
                                val nick = params[1].lowercase()
                                val account = params[2]
                                pendingWhois[nick]?.add("Account: $account")

                                if (!silentWhoisTargets.contains(nick)) {
                                    val isSelf =
                                            nick.equals(getCurrentNickname(), ignoreCase = true)
                                    _messages.emit(
                                            IncomingMessage(
                                                    target =
                                                            if (isSelf) serverChannelName
                                                            else params[1],
                                                    sender = "WHOIS",
                                                    content = "Account: $account",
                                                    type = MessageType.SERVER
                                            )
                                    )
                                }
                            }
                        }
                        671 -> { // RPL_WHOISSECURE
                            val params = event.parameters
                            if (params.size >= 2) {
                                val nick = params[1].lowercase()
                                pendingWhois[nick]?.add("Using TLS")

                                if (!silentWhoisTargets.contains(nick)) {
                                    val isSelf =
                                            nick.equals(getCurrentNickname(), ignoreCase = true)
                                    _messages.emit(
                                            IncomingMessage(
                                                    target =
                                                            if (isSelf) serverChannelName
                                                            else params[1],
                                                    sender = "WHOIS",
                                                    content = "Using TLS",
                                                    type = MessageType.SERVER
                                            )
                                    )
                                }
                            }
                        }
                        313 -> { // RPL_WHOISOPERATOR
                            val params = event.parameters
                            if (params.size >= 2) {
                                val nick = params[1].lowercase()
                                pendingWhois[nick]?.add("IRC Operator")

                                if (!silentWhoisTargets.contains(nick)) {
                                    val isSelf =
                                            nick.equals(getCurrentNickname(), ignoreCase = true)
                                    _messages.emit(
                                            IncomingMessage(
                                                    target =
                                                            if (isSelf) serverChannelName
                                                            else params[1],
                                                    sender = "WHOIS",
                                                    content = "is an IRC Operator",
                                                    type = MessageType.SERVER
                                            )
                                    )
                                }
                            }
                        }
                        301 -> { // RPL_AWAY
                            val params = event.parameters
                            if (params.size >= 3) {
                                val nick = params[1].lowercase()
                                val awayMsg = params.drop(2).joinToString(" ")
                                pendingWhois[nick]?.add("Away: $awayMsg")

                                if (!silentWhoisTargets.contains(nick)) {
                                    val isSelf =
                                            nick.equals(getCurrentNickname(), ignoreCase = true)
                                    _messages.emit(
                                            IncomingMessage(
                                                    target =
                                                            if (isSelf) serverChannelName
                                                            else params[1],
                                                    sender = "WHOIS",
                                                    content = "Away: $awayMsg",
                                                    type = MessageType.SERVER
                                            )
                                    )
                                }
                            }
                        }
                        318 -> { // RPL_ENDOFWHOIS - finalize and cache
                            val params = event.parameters
                            if (params.size >= 2) {
                                val nick = params[1].lowercase()
                                val lines = pendingWhois.remove(nick)
                                if (!lines.isNullOrEmpty()) {
                                    val whoisInfo = lines.joinToString("\n")
                                    _whoisCache.update { it + (nick to whoisInfo) }
                                }

                                val wasSilent = silentWhoisTargets.remove(nick)
                                if (!wasSilent) {
                                    // Also emit message to PM window (or server if self)
                                    val isSelf =
                                            nick.equals(getCurrentNickname(), ignoreCase = true)
                                    _messages.emit(
                                            IncomingMessage(
                                                    target =
                                                            if (isSelf) serverChannelName
                                                            else params[1],
                                                    sender = "WHOIS",
                                                    content = "End of WHOIS for ${params[1]}",
                                                    type = MessageType.SERVER
                                            )
                                    )
                                }
                            }
                        }
                        // Other WHOIS numerics - attempt to redirect if we can identify nick,
                        // otherwise server
                        307,
                        314,
                        369,
                        378,
                        379,
                        276 -> {
                            val target =
                                    if (event.parameters.size > 1) event.parameters[1]
                                    else serverChannelName
                            val message = event.parameters.drop(1).joinToString(" ")

                            // Heuristic: check if second param is a nick we are tracking or if we
                            // opened a query
                            // Not perfect, but fallback to server channel is fine.
                            // For simplicity, we keep these on Server Channel unless we are sure.
                            // 276 is WHOIS CERT fingerprint
                            // 378 is WHOIS HOST (real ip)

                            val finalTarget =
                                    if (silentWhoisTargets.contains(target.lowercase())) {
                                        serverChannelName // Suppressed anyway? No,
                                        // silentWhoisTargets doesn't suppress
                                        // these generic ones in original code
                                        // logic, but maybe should?
                                        // Original code said: "For now, we'll let them through"
                                        serverChannelName
                                    } else if (_channels.value.containsKey(target)) {
                                        target
                                    } else {
                                        serverChannelName
                                    }

                            if (!silentWhoisTargets.contains(target.lowercase())) {
                                _messages.emit(
                                        IncomingMessage(
                                                target = finalTarget,
                                                sender = "Server",
                                                content = message,
                                                type = MessageType.SYSTEM
                                        )
                                )
                            }
                        }

                        // Server Information / MOTD / Welcome
                        // Note: Numeric 1 (RPL_WELCOME) has its own dedicated handler below for
                        // auto-join
                        2,
                        3,
                        4,
                        5, // Welcome sequence (excluding 1 which is handled separately)
                        251,
                        252,
                        253,
                        254,
                        255,
                        265,
                        266, // LUSERS
                        372,
                        375,
                        376 // MOTD
                        -> {
                            val content = event.parameters.drop(1).joinToString(" ")
                            _messages.emit(
                                    IncomingMessage(
                                            target = serverChannelName,
                                            sender = "Server",
                                            content = content,
                                            type = MessageType.SERVER
                                    )
                            )
                        }

                        // Handle Nickname In Use (433) specifically
                        433 -> {
                            val rejectedNick = event.parameters.getOrNull(1) ?: config.nickname
                            val newNick =
                                    if (rejectedNick == config.nickname &&
                                                    config.altNickname.isNotBlank()
                                    ) {
                                        config.altNickname
                                    } else {
                                        // Sanitize backticks (often added by Kitteh lib default) to
                                        // underscores
                                        val sanitized = rejectedNick.replace('`', '_')
                                        if (sanitized != rejectedNick) {
                                            sanitized
                                        } else {
                                            rejectedNick + "_"
                                        }
                                    }

                            Logger.d(
                                    "IrcClient",
                                    "Nickname $rejectedNick in use, retrying with $newNick"
                            )
                            client?.sendRawLine("NICK $newNick")

                            _messages.emit(
                                    IncomingMessage(
                                            target = serverChannelName,
                                            sender = "Client",
                                            content =
                                                    "Nickname '$rejectedNick' is unavailable. Retrying with '$newNick'...",
                                            type = MessageType.SYSTEM
                                    )
                            )
                        }

                        // General Errors
                        in 400..599 -> {
                            val target = event.parameters.getOrNull(0) ?: "*"
                            val message = event.parameters.drop(1).joinToString(" ")
                            _messages.emit(
                                    IncomingMessage(
                                            target = serverChannelName,
                                            sender = "Error",
                                            content = "$target: $message (Code: $numeric)",
                                            type = MessageType.ERROR
                                    )
                            )
                        }
                        1 -> { // RPL_WELCOME
                            if (!hasConnectedBefore) {
                                hasConnectedBefore = true
                            }

                            val autoJoinChannelList =
                                    if (config.autoJoinChannels.isNotBlank()) {
                                        config.autoJoinChannels
                                                .split(",")
                                                .map { it.trim() }
                                                .filter { it.isNotBlank() }
                                                .map { if (it.startsWith("#")) it else "#$it" }
                                    } else {
                                        emptyList()
                                    }

                            // NickServ Identification
                            if (!config.nickServPassword.isNullOrBlank()) {
                                val password = config.nickServPassword!!
                                val commandTemplate = config.nickServCommand.ifBlank { "IDENTIFY" }

                                val msg =
                                        if (commandTemplate.contains("{password}")) {
                                            commandTemplate
                                                    .replace("{password}", password)
                                                    .replace("{nick}", config.nickname)
                                        } else {
                                            "$commandTemplate $password"
                                        }

                                sendRawCommand("PRIVMSG NickServ :$msg")
                                _messages.emit(
                                        IncomingMessage(
                                                target = serverChannelName,
                                                sender = "Client",
                                                content = "Sent identification to NickServ",
                                                type = MessageType.NOTICE,
                                                isSelf = true
                                        )
                                )
                            }

                            // Execute On-Connect Commands
                            if (config.onConnectCommands.isNotBlank()) {
                                val commands =
                                        config.onConnectCommands
                                                .split("\n")
                                                .map { it.trim() }
                                                .filter { it.isNotBlank() }

                                commands.forEach { commandStr ->
                                    // Ensure it starts with / for the parser, although the user
                                    // input usually does
                                    val cmdToParse =
                                            if (commandStr.startsWith("/")) commandStr
                                            else "/$commandStr"

                                    try {
                                        when (val command = IrcCommandParser.parse(cmdToParse)) {
                                            is IrcCommand.Message ->
                                                    sendMessage(command.target, command.message)
                                            is IrcCommand.Raw -> sendRawCommand(command.command)
                                            is IrcCommand.Join -> joinChannel(command.channel)
                                            is IrcCommand.Nick ->
                                                    sendRawCommand("NICK ${command.newNick}")
                                            is IrcCommand.Mode ->
                                                    sendRawCommand("MODE ${command.modeString}")
                                            is IrcCommand.Whois ->
                                                    sendRawCommand("WHOIS ${command.nickname}")
                                            is IrcCommand.Invite ->
                                                    sendRawCommand(
                                                            "INVITE ${command.nickname} ${command.channel ?: ""}"
                                                    )
                                            is IrcCommand.Notice ->
                                                    sendRawCommand(
                                                            "NOTICE ${command.target} :${command.message}"
                                                    )
                                            // Fallback for others to try sending as raw line if
                                            // possible, or just raw
                                            is IrcCommand.Unknown,
                                            is IrcCommand.NotACommand -> {
                                                // If unknown, strip slash and send as raw line
                                                // (best guess)
                                                val rawLine =
                                                        if (commandStr.startsWith("/"))
                                                                commandStr.substring(1)
                                                        else commandStr
                                                sendRawCommand(rawLine)
                                            }
                                            else -> {
                                                // Try to formulate a raw command for other types if
                                                // they are simple
                                                // Or just warn? For now, let's try to pass the raw
                                                // string if we can't handle it
                                                val rawLine =
                                                        if (commandStr.startsWith("/"))
                                                                commandStr.substring(1)
                                                        else commandStr
                                                sendRawCommand(rawLine)
                                            }
                                        }
                                        // Brief delay between commands to avoid flood
                                        kotlinx.coroutines.delay(500)
                                    } catch (e: Exception) {
                                        Logger.e(
                                                "IrcClient",
                                                "Failed to execute on-connect command: $commandStr",
                                                e
                                        )
                                    }
                                }
                            }

                            // Auto-Join Channels with delay to prevent flood
                            if (autoJoinChannelList.isNotEmpty()) {
                                Logger.d(
                                        "IrcClient",
                                        "Auto-joining ${autoJoinChannelList.size} channels"
                                )
                                autoJoinChannelList.forEach { channelName ->
                                    try {
                                        joinChannel(channelName)
                                        // Delay between joins to prevent excessive flooding
                                        kotlinx.coroutines.delay(500)
                                    } catch (e: Exception) {
                                        Logger.e(
                                                "IrcClient",
                                                "Failed to auto-join channel: $channelName",
                                                e
                                        )
                                    }
                                }
                            }
                        }
                        372, 375, 376 -> {
                            if (!hasConnectedBefore) {
                                val params = event.parameters
                                if (params.size >= 2) {
                                    val message = params.last()
                                    _messages.emit(
                                            IncomingMessage(
                                                    target = serverChannelName,
                                                    sender = "MOTD",
                                                    content = message,
                                                    type = MessageType.SERVER
                                            )
                                    )
                                }
                            }
                        }
                    }
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onNumericEvent", e)
            }
        }
    }
}
