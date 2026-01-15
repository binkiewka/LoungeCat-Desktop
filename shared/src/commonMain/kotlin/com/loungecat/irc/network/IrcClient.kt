package com.loungecat.irc.network

import com.loungecat.irc.data.model.*
import com.loungecat.irc.util.IrcCommand
import com.loungecat.irc.util.IrcCommandParser
import com.loungecat.irc.util.Logger
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch
import net.engio.mbassy.listener.Handler
import org.kitteh.irc.client.library.Client
import org.kitteh.irc.client.library.element.Channel as KittehChannel
import org.kitteh.irc.client.library.element.User
import org.kitteh.irc.client.library.event.channel.*
import org.kitteh.irc.client.library.event.client.ClientReceiveNumericEvent
import org.kitteh.irc.client.library.event.connection.ClientConnectionEndedEvent
import org.kitteh.irc.client.library.event.connection.ClientConnectionEstablishedEvent
import org.kitteh.irc.client.library.event.user.PrivateMessageEvent
import org.kitteh.irc.client.library.event.user.UserNickChangeEvent
import org.kitteh.irc.client.library.event.user.UserQuitEvent

class IrcClient(
        private var config: ServerConfig,
        private val serverId: Long,
        private val scope: CoroutineScope
) {
    private var client: Client? = null
    private var eventListener: EventListener? = null

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Connecting)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _messages = MutableSharedFlow<IncomingMessage>()
    val messages: SharedFlow<IncomingMessage> = _messages.asSharedFlow()

    private val _channels = MutableStateFlow<Map<String, Channel>>(emptyMap())
    val channels: StateFlow<Map<String, Channel>> = _channels.asStateFlow()

    private val previousTopics = mutableMapOf<String, String?>()
    private var hasConnectedBefore = false
    private val initialJoinChannels = mutableSetOf<String>()

    suspend fun connect() {
        try {
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
                        } catch (e: Exception) {}
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

    fun updateConfig(newConfig: ServerConfig) {
        this.config = newConfig
    }

    fun disconnect() {
        // Unregister event listener before shutdown to prevent ghost events
        eventListener?.let { listener -> client?.eventManager?.unregisterEventListener(listener) }
        eventListener = null
        client?.shutdown("Disconnecting")
        client = null
        _connectionState.value = ConnectionState.Disconnected
    }

    suspend fun sendMessage(target: String, message: String) {
        client?.sendMessage(target, message)
        _messages.emit(
                IncomingMessage(
                        target = target,
                        sender = config.nickname,
                        content = message,
                        type = MessageType.NORMAL,
                        isSelf = true
                )
        )
    }

    suspend fun joinChannel(channelName: String) {
        client?.addChannel(channelName)
    }

    suspend fun partChannel(channelName: String) {
        client?.removeChannel(channelName)
    }

    suspend fun sendRawCommand(command: String) {
        client?.sendRawLine(command)
    }

    fun getCurrentNickname(): String = client?.nick ?: config.nickname

    inner class EventListener {

        @Handler
        fun onConnectionEstablished(event: ClientConnectionEstablishedEvent) {
            Logger.d("IrcClient", ">>> EVENT: ClientConnectionEstablishedEvent RECEIVED")
            try {
                scope.launch {
                    _connectionState.value = ConnectionState.Connected(config.serverName)

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

                    if (event.user.nick == client?.nick) {
                        initialJoinChannels.add(channelName)
                        _channels.update { channels ->
                            channels +
                                    (channelName to
                                            Channel(name = channelName, type = ChannelType.CHANNEL))
                        }
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
                    updateChannelUsers(event.channel)
                    initialJoinChannels.remove(channelName)
                }
            } catch (e: Exception) {
                Logger.e("IrcClient", ">>> EVENT HANDLER CRASHED: onChannelUsersUpdated", e)
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
                val users =
                        channel.users.map { user ->
                            ChannelUser(nickname = user.nick, modes = getUserModes(channel, user))
                        }

                _channels.update { channels ->
                    val existingChannel = channels[channel.name]
                    if (existingChannel != null) {
                        channels + (channel.name to existingChannel.copy(users = users))
                    } else {
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

                    when (numeric) {
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
                                        kotlinx.coroutines.delay(200)
                                    } catch (e: Exception) {
                                        Logger.e(
                                                "IrcClient",
                                                "Failed to execute on-connect command: $commandStr",
                                                e
                                        )
                                    }
                                }
                            }

                            // Auto-Join Channels
                            autoJoinChannelList.forEach { channelName -> joinChannel(channelName) }
                        }
                        311 -> {
                            val params = event.parameters
                            if (params.size >= 6) {
                                val nick = params[1]
                                val user = params[2]
                                val host = params[3]
                                val realName = params[5]
                                _messages.emit(
                                        IncomingMessage(
                                                target = serverChannelName,
                                                sender = "WHOIS",
                                                content = "$nick ($user@$host): $realName",
                                                type = MessageType.SERVER
                                        )
                                )
                            }
                        }
                        318 -> {
                            val params = event.parameters
                            if (params.size >= 2) {
                                val nick = params[1]
                                _messages.emit(
                                        IncomingMessage(
                                                target = serverChannelName,
                                                sender = "WHOIS",
                                                content = "End of WHOIS for $nick",
                                                type = MessageType.SERVER
                                        )
                                )
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
