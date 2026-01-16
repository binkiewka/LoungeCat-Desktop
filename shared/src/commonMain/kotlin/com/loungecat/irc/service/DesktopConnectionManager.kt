package com.loungecat.irc.service

import com.loungecat.irc.data.model.*
import com.loungecat.irc.network.IrcClient
import com.loungecat.irc.util.HighlightMatcher
import com.loungecat.irc.util.ImageUrlDetector
import com.loungecat.irc.util.IrcCommand
import com.loungecat.irc.util.IrcCommandParser
import com.loungecat.irc.util.Logger
import com.loungecat.irc.util.UrlExtractor
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

class DesktopConnectionManager {

    private val connectionMutex = Mutex()
    // ... rest

    private val managerScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    private val _connections = MutableStateFlow<Map<Long, ServerConnection>>(emptyMap())
    val connectionStates: StateFlow<Map<Long, ServerConnection>> = _connections.asStateFlow()

    private val connections: Map<Long, ServerConnection>
        get() = _connections.value

    private val _savedConfigs = MutableStateFlow<List<ServerConfig>>(emptyList())
    val savedConfigs: StateFlow<List<ServerConfig>> = _savedConfigs.asStateFlow()

    private val _currentServerId = MutableStateFlow<Long?>(null)
    val currentServerId: StateFlow<Long?> = _currentServerId.asStateFlow()

    private val _servers = MutableStateFlow<List<ServerListItem>>(emptyList())
    val servers: StateFlow<List<ServerListItem>> = _servers.asStateFlow()

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _channels = MutableStateFlow<List<Channel>>(emptyList())
    val channels: StateFlow<List<Channel>> = _channels.asStateFlow()

    private val _messages = MutableStateFlow<Map<String, List<IncomingMessage>>>(emptyMap())
    val messages: StateFlow<Map<String, List<IncomingMessage>>> = _messages.asStateFlow()

    private val _currentChannel = MutableStateFlow<String?>(null)
    val currentChannel: StateFlow<String?> = _currentChannel.asStateFlow()

    private val _urlPreviews = MutableStateFlow<Map<String, List<UrlPreview>>>(emptyMap())
    val urlPreviews: StateFlow<Map<String, List<UrlPreview>>> = _urlPreviews.asStateFlow()

    private val _imageUrls = MutableStateFlow<Map<String, List<String>>>(emptyMap())
    val imageUrls: StateFlow<Map<String, List<String>>> = _imageUrls.asStateFlow()

    private val _showJoinPartMessages = MutableStateFlow(true)
    val showJoinPartMessages: StateFlow<Boolean> = _showJoinPartMessages.asStateFlow()

    // User preferences state
    private val _userPreferences = MutableStateFlow(UserPreferences())
    val userPreferences: StateFlow<UserPreferences> = _userPreferences.asStateFlow()

    // Reconnect job tracking
    private val reconnectJobs = mutableMapOf<Long, Job>()

    // Connection listener jobs tracking
    private val connectionListenerJobs = mutableMapOf<Long, List<Job>>()

    // Message cache callback (set by platform-specific code)
    var messageCacheLoader: ((Long, String) -> List<IncomingMessage>)? = null
    var messageCacheSaver: ((Long, String, List<IncomingMessage>) -> Unit)? = null
    var messageCachePaginatedLoader: ((Long, String, Int, Int) -> List<IncomingMessage>)? = null
    var messageCacheCountLoader: ((Long, String) -> Int)? = null

    // Track loading state for scrollback
    private val _isLoadingOlderMessages = MutableStateFlow(false)
    val isLoadingOlderMessages: StateFlow<Boolean> = _isLoadingOlderMessages.asStateFlow()

    fun setShowJoinPartMessages(show: Boolean) {
        _showJoinPartMessages.value = show
        updatePreference { it.copy(showJoinPartMessages = show) }
    }

    fun setFontSize(fontSize: FontSize) {
        updatePreference { it.copy(fontSize = fontSize) }
    }

    fun setTimestampFormat(format: TimestampFormat) {
        updatePreference { it.copy(timestampFormat = format) }
    }

    fun setUrlImageDisplayMode(mode: UrlImageDisplayMode) {
        updatePreference { it.copy(urlImageDisplayMode = mode) }
    }

    fun setColoredNicknames(enabled: Boolean) {
        updatePreference { it.copy(coloredNicknames = enabled) }
    }

    fun setSplitViewEnabled(enabled: Boolean) {
        updatePreference { it.copy(splitViewEnabled = enabled) }
    }

    fun setSpellCheckEnabled(enabled: Boolean) {
        updatePreference { it.copy(spellCheckEnabled = enabled) }
    }

    fun setSpellCheckLanguage(language: String) {
        updatePreference { it.copy(spellCheckLanguage = language) }
    }

    fun setTheme(theme: String) {
        updatePreference { it.copy(theme = theme) }
    }

    // Sound Alerts
    fun setSoundAlertsEnabled(enabled: Boolean) {
        updatePreference { it.copy(soundAlertsEnabled = enabled) }
    }

    fun setSoundVolume(volume: Float) {
        updatePreference { it.copy(soundVolume = volume) }
    }

    fun setSoundOnMention(enabled: Boolean) {
        updatePreference { it.copy(soundOnMention = enabled) }
    }

    fun setSoundOnPrivateMessage(enabled: Boolean) {
        updatePreference { it.copy(soundOnPrivateMessage = enabled) }
    }

    fun setSoundOnHighlight(enabled: Boolean) {
        updatePreference { it.copy(soundOnHighlight = enabled) }
    }

    // AI Assistant

    fun loadPreferences(prefs: UserPreferences) {
        _userPreferences.value = prefs
        _showJoinPartMessages.value = prefs.showJoinPartMessages
        ignoredUsers = prefs.ignoredUsers
    }

    private var preferencesUpdater: ((UserPreferences) -> Unit)? = null

    fun setPreferencesUpdater(updater: (UserPreferences) -> Unit) {
        preferencesUpdater = updater
    }

    private fun updatePreference(updater: (UserPreferences) -> UserPreferences) {
        _userPreferences.value = updater(_userPreferences.value)
        preferencesUpdater?.invoke(_userPreferences.value)
    }

    private val _mentionEvents = MutableSharedFlow<Triple<String, String, String>>()
    val mentionEvents: SharedFlow<Triple<String, String, String>> = _mentionEvents.asSharedFlow()

    private val _highlightEvents = MutableSharedFlow<Triple<String, String, String>>()
    val highlightEvents: SharedFlow<Triple<String, String, String>> =
            _highlightEvents.asSharedFlow()

    private val _pmEvents = MutableSharedFlow<Pair<String, String>>()
    val pmEvents: SharedFlow<Pair<String, String>> = _pmEvents.asSharedFlow()

    fun getIgnoredUsers(): Set<String> = ignoredUsers

    private var ignoredUsers: Set<String> = emptySet()

    fun ignoreUser(nickname: String) {
        val lowerNick = nickname.lowercase()
        if (ignoredUsers.contains(lowerNick)) return

        ignoredUsers = ignoredUsers + lowerNick
        updatePreference { it.copy(ignoredUsers = ignoredUsers) }
    }

    fun removeIgnoredUser(nickname: String) {
        val lowerNick = nickname.lowercase()
        if (!ignoredUsers.contains(lowerNick)) return

        ignoredUsers = ignoredUsers - lowerNick
        updatePreference { it.copy(ignoredUsers = ignoredUsers) }
    }
    private var currentNickname: String = ""

    private val urlPreviewService = LinkPreviewService()

    private var databaseService: DatabaseServiceInterface? = null

    fun setDatabaseService(service: DatabaseServiceInterface) {
        databaseService = service
    }

    suspend fun loadSavedServers() {
        databaseService?.let { db ->
            val configs = db.getAllServerConfigs()
            _savedConfigs.value = configs
            Logger.d("DesktopConnectionManager", "Loaded ${configs.size} saved server configs")
        }
    }

    private suspend fun saveServerConfig(config: ServerConfig) {
        databaseService?.let { db ->
            db.insertServerConfig(config)
            Logger.d("DesktopConnectionManager", "Saved server config: ${config.serverName}")
        }
    }

    private suspend fun deleteServerConfig(id: Long) {
        databaseService?.let { db ->
            db.deleteServerConfig(id)
            Logger.d("DesktopConnectionManager", "Deleted server config: $id")
        }
    }

    private fun updateSavedChannels(serverId: Long, channelName: String, isJoining: Boolean) {
        managerScope.launch {
            val connection = connections[serverId] ?: return@launch
            val currentConfig = connection.config

            val channelToUpdate = channelName.trim()
            if (channelToUpdate.isEmpty()) return@launch

            val currentChannels =
                    currentConfig
                            .autoJoinChannels
                            .split(",")
                            .map { it.trim() }
                            .filter { it.isNotEmpty() }
                            .toMutableSet()

            var changed = false
            if (isJoining) {
                // Simple duplicate check, though Set handles it.
                // We rely on the server's channel name casing usually, but here we just save what
                // we requested.
                if (currentChannels.add(channelToUpdate)) changed = true
            } else {
                if (currentChannels.remove(channelToUpdate)) changed = true
            }

            if (changed) {
                val newChannelsString = currentChannels.joinToString(",")
                val newConfig = currentConfig.copy(autoJoinChannels = newChannelsString)

                _connections.update { current ->
                    val serverConn = current[serverId] ?: return@update current
                    current + (serverId to serverConn.copy(config = newConfig))
                }

                databaseService?.let { db ->
                    db.updateServerConfig(newConfig)
                    Logger.d(
                            "DesktopConnectionManager",
                            "Updated saved channels for ${newConfig.serverName}: $newChannelsString"
                    )
                }
            }
        }
    }

    fun getCurrentNickname(): String {
        val serverId = _currentServerId.value ?: return ""
        return connections[serverId]?.config?.nickname ?: ""
    }

    fun connect(config: ServerConfig, saveToDatabase: Boolean = true, isNew: Boolean = false) {
        // Ensure autoConnect is true when manually connecting
        val configToUse = config.copy(autoConnect = true)

        Logger.d("DesktopConnectionManager", "=== CONNECT REQUEST ===")
        Logger.d(
                "DesktopConnectionManager",
                "Server ID: ${configToUse.id}, Name: ${configToUse.serverName}, isNew: $isNew"
        )
        managerScope.launch {
            connectionMutex.withLock {
                try {
                    val existingConnection = connections[configToUse.id]

                    // Prevent duplicate connection attempts if already connected or connecting
                    if (existingConnection != null) {
                        // If we are asked to save, we should check if the config has changed
                        if (saveToDatabase) {
                            if (configToUse.id != 0L && !isNew) {
                                databaseService?.updateServerConfig(configToUse)
                                Logger.d(
                                        "DesktopConnectionManager",
                                        "Updated server config in DB: ${configToUse.serverName}"
                                )
                            }
                        }

                        if (existingConnection.isConnected() ||
                                        existingConnection.connectionState is
                                                ConnectionState.Connecting
                        ) {
                            // Check if config effectively changed (ignoring transient fields if
                            // any)
                            // We compare relevant fields. Data class equals should be enough for
                            // now.
                            // Note: configToUse has autoConnect=true, we should match that or
                            // ignore it.
                            val currentConfig = existingConnection.config.copy(autoConnect = true)
                            if (currentConfig == configToUse) {
                                Logger.d(
                                        "DesktopConnectionManager",
                                        "Already connected with same config, switching view"
                                )
                                switchToServer(configToUse.id)
                                return@launch
                            } else {
                                Logger.d(
                                        "DesktopConnectionManager",
                                        "Config changed while connected, reconnecting..."
                                )
                                // We proceed to disconnect below...
                            }
                        }

                        // Cleanup existing connection (ensure Kitteh client is stopped)
                        try {
                            Logger.d(
                                    "DesktopConnectionManager",
                                    "Cleaning up old connection for server ${configToUse.id}"
                            )
                            existingConnection.client.disconnect()
                        } catch (e: Exception) {
                            Logger.e(
                                    "DesktopConnectionManager",
                                    "Error disconnecting existing client",
                                    e
                            )
                        }

                        // Cancel old listener jobs to prevent duplicates
                        connectionListenerJobs[configToUse.id]?.forEach { it.cancel() }
                        connectionListenerJobs.remove(configToUse.id)

                        // Wait for Kitteh client to fully cleanup before creating new connection
                        delay(1000)
                    }

                    if (saveToDatabase) {
                        // Decide whether to insert or update based on ID
                        // If it's explicitly new, we MUST save (insert) regardless of the random ID
                        // generated by UI
                        if (configToUse.id != 0L && !isNew) {
                            databaseService?.updateServerConfig(configToUse)
                            Logger.d(
                                    "DesktopConnectionManager",
                                    "Updated server config: ${configToUse.serverName}"
                            )
                        } else {
                            saveServerConfig(configToUse)
                        }
                    }

                    updateConnectionState(configToUse.id, ConnectionState.Connecting)

                    val client = IrcClient(configToUse, configToUse.id, managerScope)
                    val connection =
                            ServerConnection(
                                    serverId = configToUse.id,
                                    config = configToUse,
                                    client = client,
                                    connectionState = ConnectionState.Connecting,
                                    manuallyDisconnected = false
                            )

                    _connections.update { it + (configToUse.id to connection) }

                    // Track new listener jobs
                    val jobs = setupClientListeners(configToUse.id, client)
                    connectionListenerJobs[configToUse.id] = jobs

                    client.connect()
                    switchToServer(configToUse.id)
                    updateServerList()
                } catch (e: Exception) {
                    Logger.e("DesktopConnectionManager", "Connection failed", e)
                    updateConnectionState(
                            configToUse.id,
                            ConnectionState.Error(e.message ?: "Connection failed")
                    )
                }
            }
        }
    }

    fun disconnectServer(serverId: Long) {
        managerScope.launch {
            connectionMutex.withLock {
                connections[serverId]?.let { connection ->
                    connection.client.disconnect()

                    // Persist autoConnect = false
                    val updatedConfig = connection.config.copy(autoConnect = false)
                    databaseService?.let { db ->
                        db.updateServerConfig(updatedConfig)
                        Logger.d(
                                "DesktopConnectionManager",
                                "Saved valid disconnect state for server $serverId"
                        )
                    }

                    val updatedConnection =
                            connection.copy(
                                    config = updatedConfig,
                                    connectionState = ConnectionState.Disconnected,
                                    channels = emptyList(),
                                    messages = emptyMap(),
                                    manuallyDisconnected = true
                            )
                    _connections.update { it + (serverId to updatedConnection) }

                    if (_currentServerId.value == serverId) {
                        _connectionState.value = ConnectionState.Disconnected
                        _channels.value = emptyList()
                        _messages.value = emptyMap()
                        _currentChannel.value = null
                    }

                    updateServerList()
                }
            }
        }
    }

    fun connectServer(serverId: Long) {
        managerScope.launch {
            connections[serverId]?.let { connection -> connect(connection.config) }
        }
    }

    fun removeServer(serverId: Long) {
        managerScope.launch {
            connectionMutex.withLock {
                connections[serverId]?.let { connection ->
                    connection.client.disconnect()
                    _connections.update { it - serverId }

                    deleteServerConfig(serverId)

                    if (_currentServerId.value == serverId) {
                        val nextServer = connections.keys.firstOrNull()
                        if (nextServer != null) {
                            switchToServer(nextServer)
                        } else {
                            _currentServerId.value = null
                            _connectionState.value = ConnectionState.Disconnected
                            _channels.value = emptyList()
                            _messages.value = emptyMap()
                            _currentChannel.value = null
                        }
                    }

                    // Cancel listeners
                    connectionListenerJobs[serverId]?.forEach { it.cancel() }
                    connectionListenerJobs.remove(serverId)

                    // Cancel reconnect jobs
                    reconnectJobs[serverId]?.cancel()
                    reconnectJobs.remove(serverId)

                    updateServerList()
                }
            }
        }
    }

    fun disconnectAll() {
        connections.values.forEach { it.client.disconnect() }
        _connections.value = emptyMap()
        _currentServerId.value = null
        _connectionState.value = ConnectionState.Disconnected
        _channels.value = emptyList()
        _messages.value = emptyMap()
        _currentChannel.value = null
        updateServerList()
    }

    fun switchToServer(serverId: Long) {
        connections[serverId]?.let { connection ->
            _currentServerId.value = serverId
            _connectionState.value = connection.connectionState
            _channels.value = connection.channels
            _messages.value = connection.messages
            _currentChannel.value = connection.currentChannel
        }
    }

    fun joinChannel(channelName: String) {
        val serverId = _currentServerId.value ?: return
        joinChannel(serverId, channelName)
    }

    fun joinChannel(serverId: Long, channelName: String) {
        connections[serverId]?.let { connection ->
            managerScope.launch { connection.client.joinChannel(channelName) }
            updateSavedChannels(serverId, channelName, true)
        }
    }

    fun partChannel(channelName: String) {
        val serverId = _currentServerId.value ?: return
        partChannel(serverId, channelName)
    }

    fun partChannel(serverId: Long, channelName: String) {
        connections[serverId]?.let { connection ->
            if (channelName.startsWith("#") ||
                            channelName.startsWith("&") ||
                            channelName.startsWith("+") ||
                            channelName.startsWith("!")
            ) {
                managerScope.launch { connection.client.partChannel(channelName) }
                updateSavedChannels(serverId, channelName, false)
            } else {
                // It's a query window, just close it locally
                managerScope.launch { connection.client.closeQuery(channelName) }
            }
        }
    }

    fun sendMessage(serverId: Long, channelName: String, message: String) {
        connections[serverId]?.let { _ ->
            managerScope.launch { processCommand(serverId, channelName, message) }
        }
    }

    fun sendMessage(message: String) {
        // Reset AFK timer on user activity

        val serverId = _currentServerId.value ?: return
        val channelName = _currentChannel.value

        managerScope.launch { processCommand(serverId, channelName, message) }
    }

    private suspend fun processCommand(serverId: Long, channelName: String?, message: String) {
        val connection = connections[serverId] ?: return
        when (val command = IrcCommandParser.parse(message)) {
            is IrcCommand.NotACommand -> {
                if (channelName != null) {
                    sendMessageOrUsePastebin(connection, channelName, message)
                }
            }
            is IrcCommand.Join -> joinChannel(serverId, command.channel)
            is IrcCommand.Part -> {
                val target = command.channel ?: channelName
                if (target != null) {
                    if (target.startsWith("#") ||
                                    target.startsWith("&") ||
                                    target.startsWith("+") ||
                                    target.startsWith("!")
                    ) {
                        connection.client.sendRawCommand(
                                "PART $target :${command.message ?: "Leaving"}"
                        )
                        updateSavedChannels(serverId, target, false)
                    } else {
                        // Close query locally
                        connection.client.closeQuery(target)
                    }
                }
            }
            is IrcCommand.Message ->
                    sendMessageOrUsePastebin(connection, command.target, command.message)
            is IrcCommand.Action -> {
                if (channelName != null) {
                    connection.client.sendMessage(
                            channelName,
                            "\u0001ACTION ${command.action}\u0001"
                    )
                }
            }
            is IrcCommand.Nick -> connection.client.sendRawCommand("NICK ${command.newNick}")
            is IrcCommand.Identify -> {
                val msg = "IDENTIFY ${command.args}"
                connection.client.sendRawCommand("PRIVMSG NickServ :$msg")
                addSystemMessage(
                        serverId,
                        channelName ?: "* ${connection.config.serverName}",
                        "Sent identification to NickServ"
                )
            }
            is IrcCommand.Whois -> connection.client.sendRawCommand("WHOIS ${command.nickname}")
            is IrcCommand.Quit -> {
                val quitMsg = command.message ?: "Leaving"
                // Send QUIT first
                connection.client.sendRawCommand("QUIT :$quitMsg")
                // Then disconnect locally
                disconnectServer(serverId)
            }
            is IrcCommand.Raw -> connection.client.sendRawCommand(command.command)
            is IrcCommand.Clear -> {
                if (channelName != null) {
                    clearChannelHistory(serverId, channelName)
                }
            }
            is IrcCommand.Help -> {
                if (channelName != null) {
                    addSystemMessage(serverId, channelName, IrcCommandParser.getHelpText())
                }
            }
            is IrcCommand.Ignore -> {
                ignoreUser(command.nickname)
                if (channelName != null) {
                    addSystemMessage(serverId, channelName, "Ignored user: ${command.nickname}")
                }
            }
            is IrcCommand.Unignore -> {
                removeIgnoredUser(command.nickname)
                if (channelName != null) {
                    addSystemMessage(serverId, channelName, "Unignored user: ${command.nickname}")
                }
            }
            is IrcCommand.Kick -> {
                if (channelName != null) {
                    val kickCmd =
                            if (command.reason != null) {
                                "KICK $channelName ${command.nickname} :${command.reason}"
                            } else {
                                "KICK $channelName ${command.nickname}"
                            }
                    connection.client.sendRawCommand(kickCmd)
                }
            }
            is IrcCommand.Ban -> {
                if (channelName != null) {
                    connection.client.sendRawCommand("MODE $channelName +b ${command.nickname}!*@*")
                }
            }
            is IrcCommand.Unban -> {
                if (channelName != null) {
                    connection.client.sendRawCommand("MODE $channelName -b ${command.nickname}!*@*")
                }
            }
            is IrcCommand.Voice -> {
                if (channelName != null) {
                    connection.client.sendRawCommand("MODE $channelName +v ${command.nickname}")
                }
            }
            is IrcCommand.Devoice -> {
                if (channelName != null) {
                    connection.client.sendRawCommand("MODE $channelName -v ${command.nickname}")
                }
            }
            is IrcCommand.Op -> {
                if (channelName != null) {
                    connection.client.sendRawCommand("MODE $channelName +o ${command.nickname}")
                }
            }
            is IrcCommand.Deop -> {
                if (channelName != null) {
                    connection.client.sendRawCommand("MODE $channelName -o ${command.nickname}")
                }
            }
            is IrcCommand.Mode -> {
                if (channelName != null) {
                    connection.client.sendRawCommand("MODE $channelName ${command.modeString}")
                }
            }
            is IrcCommand.Topic -> {
                if (channelName != null) {
                    if (command.topic != null) {
                        connection.client.sendRawCommand("TOPIC $channelName :${command.topic}")
                    } else {
                        connection.client.sendRawCommand("TOPIC $channelName")
                    }
                }
            }
            is IrcCommand.Invite -> {
                connection.client.sendRawCommand(
                        "INVITE ${command.nickname} ${command.channel ?: channelName ?: ""}"
                )
            }
            is IrcCommand.List -> {
                connection.client.sendRawCommand(
                        if (command.filter != null) "LIST ${command.filter}" else "LIST"
                )
            }
            is IrcCommand.Names -> {
                connection.client.sendRawCommand("NAMES ${command.channel ?: channelName ?: ""}")
            }
            is IrcCommand.Notice -> {
                connection.client.sendRawCommand("NOTICE ${command.target} :${command.message}")
            }
            is IrcCommand.Query -> {
                startPrivateMessage(command.nickname)
                if (command.message != null) {
                    sendMessage(serverId, command.nickname, command.message)
                }
            }
            is IrcCommand.Cycle -> {
                val target = command.channel ?: channelName
                if (target != null) {
                    connection.client.partChannel(target)
                    delay(200) // Brief delay to ensure part is processed
                    connection.client.joinChannel(target)
                }
            }
            is IrcCommand.Away -> {
                connection.client.sendRawCommand(
                        if (command.message != null) "AWAY :${command.message}" else "AWAY"
                )
            }
            is IrcCommand.Back -> connection.client.sendRawCommand("AWAY")
            is IrcCommand.Who -> connection.client.sendRawCommand("WHO ${command.mask}")
            is IrcCommand.Ping -> connection.client.sendRawCommand("PING ${command.target ?: ""}")
            is IrcCommand.Time -> connection.client.sendRawCommand("TIME ${command.server ?: ""}")
            is IrcCommand.Version ->
                    connection.client.sendRawCommand("VERSION ${command.server ?: ""}")
            is IrcCommand.Motd -> connection.client.sendRawCommand("MOTD ${command.server ?: ""}")
            is IrcCommand.Info -> connection.client.sendRawCommand("INFO ${command.server ?: ""}")
            is IrcCommand.NickServ -> {
                val cmd =
                        if (command.args.isNotBlank()) "PRIVMSG NickServ :${command.args}"
                        else "PRIVMSG NickServ :HELP"
                connection.client.sendRawCommand(cmd)
            }
            is IrcCommand.ChanServ -> {
                val cmd =
                        if (command.args.isNotBlank()) "PRIVMSG ChanServ :${command.args}"
                        else "PRIVMSG ChanServ :HELP"
                connection.client.sendRawCommand(cmd)
            }
            is IrcCommand.MemoServ -> {
                val cmd =
                        if (command.args.isNotBlank()) "PRIVMSG MemoServ :${command.args}"
                        else "PRIVMSG MemoServ :HELP"
                connection.client.sendRawCommand(cmd)
            }
            is IrcCommand.Links -> connection.client.sendRawCommand("LINKS ${command.server ?: ""}")
            is IrcCommand.Map -> connection.client.sendRawCommand("MAP ${command.server ?: ""}")
            is IrcCommand.Lusers -> connection.client.sendRawCommand("LUSERS ${command.mask ?: ""}")
            is IrcCommand.Admin -> connection.client.sendRawCommand("ADMIN ${command.server ?: ""}")
            is IrcCommand.Oper -> connection.client.sendRawCommand("OPER ${command.args}")
            is IrcCommand.Kill ->
                    connection.client.sendRawCommand("KILL ${command.nickname} :${command.reason}")
            is IrcCommand.KLine ->
                    connection.client.sendRawCommand("KLINE ${command.target} :${command.reason}")
            is IrcCommand.GLine ->
                    connection.client.sendRawCommand("GLINE ${command.target} :${command.reason}")
            is IrcCommand.ZLine ->
                    connection.client.sendRawCommand("ZLINE ${command.target} :${command.reason}")
            is IrcCommand.Rehash -> connection.client.sendRawCommand("REHASH ${command.args ?: ""}")
            is IrcCommand.Restart ->
                    connection.client.sendRawCommand("RESTART ${command.args ?: ""}")
            is IrcCommand.Die -> connection.client.sendRawCommand("DIE ${command.args ?: ""}")
            is IrcCommand.Wallops -> connection.client.sendRawCommand("WALLOPS :${command.message}")
            is IrcCommand.Saje ->
                    connection.client.sendRawCommand("SAJE ${command.nickname} ${command.channel}")
            is IrcCommand.KickBan -> {
                if (channelName != null) {
                    connection.client.sendRawCommand("MODE $channelName +b ${command.nickname}!*@*")
                    val kickReason = command.reason ?: "Requested"
                    connection.client.sendRawCommand(
                            "KICK $channelName ${command.nickname} :$kickReason"
                    )
                }
            }
            is IrcCommand.Ctcp -> {
                // CTCP is just a PRIVMSG with \u0001 wrapping
                connection.client.sendMessage(command.target, "\u0001${command.message}\u0001")
            }
            is IrcCommand.Unknown -> {
                if (channelName != null) {
                    addSystemMessage(serverId, channelName, "Unknown command: /${command.command}")
                }
            }
            else -> {}
        }
    }

    private suspend fun sendMessageOrUsePastebin(
            connection: ServerConnection,
            target: String,
            message: String
    ) {
        if (PastebinService.shouldPaste(message)) {
            addSystemMessage(
                    connection.serverId,
                    target,
                    "Message too long (${message.length} chars), uploading to pastebin..."
            )
            val pastedUrl = PastebinService.uploadAndFormat(message)
            if (pastedUrl != null) {
                connection.client.sendMessage(target, pastedUrl)
            } else {
                addSystemMessage(
                        connection.serverId,
                        target,
                        "Pastebin upload failed. Sending original message."
                )
                connection.client.sendMessage(target, message)
            }
        } else {
            connection.client.sendMessage(target, message)
        }
    }

    fun setCurrentChannel(channelName: String) {
        val serverId = _currentServerId.value ?: return
        connections[serverId]?.let { connection ->
            // Try to load cached messages if we don't have any for this channel
            val currentChannelMessages = connection.messages[channelName]
            val updatedMessages =
                    if (currentChannelMessages.isNullOrEmpty()) {
                        // Try to load from cache
                        val cachedMessages =
                                messageCacheLoader?.invoke(serverId, channelName) ?: emptyList()
                        if (cachedMessages.isNotEmpty()) {
                            Logger.d(
                                    "DesktopConnectionManager",
                                    "Loaded ${cachedMessages.size} cached messages for $channelName"
                            )
                            connection.messages + (channelName to cachedMessages)
                        } else {
                            connection.messages
                        }
                    } else {
                        connection.messages
                    }

            val updatedConnection =
                    connection.copy(currentChannel = channelName, messages = updatedMessages)
            _connections.update { it + (serverId to updatedConnection) }
            _currentChannel.value = channelName
            _messages.value = updatedMessages
        }
    }

    fun getChannelUsers(channelName: String): List<ChannelUser> {
        val serverId = _currentServerId.value ?: return emptyList()
        return connections[serverId]?.client?.channels?.value?.get(channelName)?.users
                ?: emptyList()
    }

    fun startPrivateMessage(nickname: String) {
        val serverId = _currentServerId.value ?: return
        managerScope.launch {
            connections[serverId]?.let { connection ->
                connection.client.startQuery(nickname)
                setCurrentChannel(nickname)
            }
        }
    }

    fun getConnection(serverId: Long): ServerConnection? = connections[serverId]

    fun getAllConnections(): List<ServerConnection> = connections.values.toList()

    fun clearChannelHistory(serverId: Long, channelName: String) {
        managerScope.launch {
            connections[serverId]?.let { connection ->
                val updatedMessages = connection.messages.toMutableMap()
                updatedMessages.remove(channelName)
                val updatedConnection = connection.copy(messages = updatedMessages)
                _connections.update { it + (serverId to updatedConnection) }

                if (_currentServerId.value == serverId) {
                    _messages.value = updatedMessages
                }
            }
        }
    }

    /**
     * Load older messages from cache when user scrolls to top. Returns the number of older messages
     * loaded.
     */
    fun loadOlderMessages(serverId: Long, channelName: String, limit: Int = 50): Int {
        if (_isLoadingOlderMessages.value) return 0

        val connection = connections[serverId] ?: return 0
        val currentMessages = connection.messages[channelName] ?: emptyList()

        // Get total cached message count
        val totalCached = messageCacheCountLoader?.invoke(serverId, channelName) ?: 0
        if (totalCached <= currentMessages.size) {
            // No more messages to load
            return 0
        }

        _isLoadingOlderMessages.value = true

        try {
            // Calculate offset to get messages older than what we have
            val offset = totalCached - currentMessages.size
            val olderMessages =
                    messageCachePaginatedLoader?.invoke(
                            serverId,
                            channelName,
                            offset.coerceAtLeast(0),
                            limit.coerceAtMost(offset)
                    )
                            ?: emptyList()

            if (olderMessages.isEmpty()) {
                return 0
            }

            // Prepend older messages to existing messages
            val combinedMessages = olderMessages + currentMessages
            val updatedMessages = connection.messages.toMutableMap()
            updatedMessages[channelName] = combinedMessages

            val updatedConnection = connection.copy(messages = updatedMessages)
            _connections.update { it + (serverId to updatedConnection) }

            if (_currentServerId.value == serverId) {
                _messages.value = updatedMessages
            }

            Logger.d(
                    "DesktopConnectionManager",
                    "Loaded ${olderMessages.size} older messages for $channelName"
            )
            return olderMessages.size
        } finally {
            _isLoadingOlderMessages.value = false
        }
    }

    fun shutdown() {
        // Cancel all reconnect jobs
        reconnectJobs.values.forEach { it.cancel() }
        reconnectJobs.clear()

        // Cancel all listener jobs
        connectionListenerJobs.values.flatten().forEach { it.cancel() }
        connectionListenerJobs.clear()

        disconnectAll()
        managerScope.cancel()
    }

    private fun setupClientListeners(serverId: Long, client: IrcClient): List<Job> {
        val jobs = mutableListOf<Job>()

        jobs.add(
                managerScope.launch {
                    client.connectionState.collect { state ->
                        updateConnectionState(serverId, state)
                    }
                }
        )

        jobs.add(
                managerScope.launch {
                    client.channels.collect { channels -> updateChannels(serverId, channels) }
                }
        )

        jobs.add(
                managerScope.launch {
                    client.messages.collect { message -> updateMessages(serverId, message) }
                }
        )

        return jobs
    }

    private fun updateConnectionState(serverId: Long, state: ConnectionState) {
        connections[serverId]?.let { connection ->
            val updatedConnection =
                    when (state) {
                        is ConnectionState.Connected -> {
                            // Reset reconnect attempts on successful connection
                            reconnectJobs[serverId]?.cancel()
                            reconnectJobs.remove(serverId)
                            connection.copy(connectionState = state, reconnectAttempt = 0)
                        }
                        is ConnectionState.Disconnected, is ConnectionState.Error -> {
                            // Schedule reconnect if not manually disconnected
                            if (!connection.manuallyDisconnected) {
                                scheduleReconnect(serverId, connection.reconnectAttempt)
                                connection.copy(
                                        connectionState = state,
                                        reconnectAttempt = connection.reconnectAttempt + 1
                                )
                            } else {
                                connection.copy(connectionState = state)
                            }
                        }
                        else -> connection.copy(connectionState = state)
                    }
            _connections.update { it + (serverId to updatedConnection) }

            if (_currentServerId.value == serverId) {
                _connectionState.value = state
            }

            updateServerList()
        }
    }

    private fun scheduleReconnect(serverId: Long, attempt: Int) {
        // Cancel any existing reconnect job for this server
        reconnectJobs[serverId]?.cancel()

        val delay =
                when (attempt) {
                    0 -> 5_000L // 5 seconds - give old connection time to close
                    1 -> 15_000L // 15 seconds
                    2 -> 30_000L // 30 seconds
                    3 -> 60_000L // 1 minute
                    else -> 300_000L // 5 minutes max
                }

        Logger.d(
                "DesktopConnectionManager",
                "Scheduling reconnect for server $serverId in ${delay}ms (attempt ${attempt + 1})"
        )

        reconnectJobs[serverId] =
                managerScope.launch {
                    delay(delay)
                    connections[serverId]?.let { connection ->
                        // Don't reconnect if manually disconnected, already connected, or currently
                        // connecting
                        if (!connection.manuallyDisconnected &&
                                        !connection.isConnected() &&
                                        connection.connectionState !is ConnectionState.Connecting
                        ) {
                            Logger.d(
                                    "DesktopConnectionManager",
                                    "Attempting reconnect for server $serverId"
                            )
                            try {
                                connection.client.connect()
                            } catch (e: Exception) {
                                Logger.e(
                                        "DesktopConnectionManager",
                                        "Reconnect failed for server $serverId",
                                        e
                                )
                            }
                        }
                    }
                }
    }

    fun cancelReconnect(serverId: Long) {
        reconnectJobs[serverId]?.cancel()
        reconnectJobs.remove(serverId)
    }

    private fun updateChannels(serverId: Long, channels: Map<String, Channel>) {
        connections[serverId]?.let { connection ->
            val channelList = channels.values.toList()
            val updatedConnection = connection.copy(channels = channelList)
            _connections.update { it + (serverId to updatedConnection) }

            if (_currentServerId.value == serverId) {
                _channels.value = channelList
            }

            updateServerList()
        }
    }

    private fun updateMessages(serverId: Long, message: IncomingMessage) {
        if (!message.isSelf && ignoredUsers.contains(message.sender.lowercase())) {
            Logger.d("DesktopConnectionManager", "Ignoring message from ${message.sender}")
            return
        }

        connections[serverId]?.let { connection ->
            val updatedMessages = connection.messages.toMutableMap()
            val targetMessages =
                    updatedMessages.getOrDefault(message.target, emptyList()).toMutableList()
            targetMessages.add(message)

            if (targetMessages.size > 500) {
                targetMessages.removeAt(0)
            }

            updatedMessages[message.target] = targetMessages

            val updatedConnection = connection.copy(messages = updatedMessages)
            _connections.update { it + (serverId to updatedConnection) }

            if (_currentServerId.value == serverId) {
                _messages.value = updatedMessages
            }

            // Save to message cache (debounced in cache implementation)
            messageCacheSaver?.invoke(serverId, message.target, targetMessages)

            if (message.type == MessageType.NORMAL) {
                processMessageUrls(message)
            }

            if (!message.isSelf && message.type == MessageType.NORMAL) {
                val nickname = connection.config.nickname
                val highlightWords = _userPreferences.value.highlightWords

                // Check for nick mention
                if (HighlightMatcher.containsMention(message.content, nickname)) {
                    managerScope.launch {
                        _mentionEvents.emit(Triple(message.target, message.sender, message.content))
                    }
                }

                // Check for custom highlight words
                if (HighlightMatcher.containsHighlight(message.content, highlightWords)) {
                    managerScope.launch {
                        _highlightEvents.emit(
                                Triple(message.target, message.sender, message.content)
                        )
                    }
                }

                if (!message.target.startsWith("#")) {
                    managerScope.launch { _pmEvents.emit(Pair(message.sender, message.content)) }
                }
            }
        }
    }

    private fun processMessageUrls(message: IncomingMessage) {
        val urls = UrlExtractor.extractUrls(message.content)
        if (urls.isEmpty()) return

        val detectedImages = mutableListOf<String>()
        val urlsToPreview = mutableListOf<String>()

        urls.forEach { url ->
            if (ImageUrlDetector.isImageUrl(url)) {
                detectedImages.add(url)
            } else {
                urlsToPreview.add(url)
            }
        }

        if (detectedImages.isNotEmpty()) {
            _imageUrls.update { current -> current + (message.id to detectedImages) }
        }

        urlsToPreview.forEach { url ->
            managerScope.launch {
                val preview = urlPreviewService.fetchPreview(url)
                _urlPreviews.update { current ->
                    val existingPreviews = current[message.id] ?: emptyList()
                    val newPreviews = existingPreviews + preview
                    current + (message.id to newPreviews)
                }
            }
        }
    }

    private fun addSystemMessage(serverId: Long, channelName: String, content: String) {
        connections[serverId]?.let { connection ->
            val systemMessage =
                    IncomingMessage(
                            target = channelName,
                            sender = "*",
                            content = content,
                            type = MessageType.SYSTEM,
                            isSelf = false
                    )

            val updatedMessages = connection.messages.toMutableMap()
            val channelMessages =
                    updatedMessages.getOrDefault(channelName, emptyList()).toMutableList()
            channelMessages.add(systemMessage)
            updatedMessages[channelName] = channelMessages

            val updatedConnection = connection.copy(messages = updatedMessages)
            _connections.update { it + (serverId to updatedConnection) }

            if (_currentServerId.value == serverId) {
                _messages.value = updatedMessages
            }
        }
    }

    private fun updateServerList() {
        _servers.value =
                connections.values.map { connection ->
                    ServerListItem(
                            serverId = connection.serverId,
                            serverName = connection.config.serverName,
                            hostname = connection.config.hostname,
                            isConnected = connection.isConnected(),
                            unreadCount = connection.getUnreadCount(),
                            channels = connection.channels
                    )
                }
    }
}

data class ServerConnection(
        val serverId: Long,
        val config: ServerConfig,
        val client: IrcClient,
        val connectionState: ConnectionState = ConnectionState.Disconnected,
        val channels: List<Channel> = emptyList(),
        val messages: Map<String, List<IncomingMessage>> = emptyMap(),
        val currentChannel: String? = null,
        val reconnectAttempt: Int = 0,
        val manuallyDisconnected: Boolean = false
) {
    fun isConnected(): Boolean = connectionState is ConnectionState.Connected
    fun getUnreadCount(): Int = channels.sumOf { it.unreadCount }
}

data class ServerListItem(
        val serverId: Long,
        val serverName: String,
        val hostname: String,
        val isConnected: Boolean,
        val unreadCount: Int,
        val channels: List<Channel>
)

interface DatabaseServiceInterface {
    suspend fun getAllServerConfigs(): List<ServerConfig>
    suspend fun insertServerConfig(config: ServerConfig): Long
    suspend fun updateServerConfig(config: ServerConfig)
    suspend fun deleteServerConfig(id: Long)
}
