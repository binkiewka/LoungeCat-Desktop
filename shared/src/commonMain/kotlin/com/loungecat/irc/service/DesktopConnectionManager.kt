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
                        if (existingConnection.isConnected() ||
                                        existingConnection.connectionState is
                                                ConnectionState.Connecting
                        ) {
                            Logger.d(
                                    "DesktopConnectionManager",
                                    "Already connected or connecting, switching to server"
                            )
                            switchToServer(configToUse.id)
                            return@launch
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
            managerScope.launch { connection.client.partChannel(channelName) }
            updateSavedChannels(serverId, channelName, false)
        }
    }

    fun sendMessage(serverId: Long, channelName: String, message: String) {
        connections[serverId]?.let { connection ->
            managerScope.launch {
                when (val command = IrcCommandParser.parse(message)) {
                    is IrcCommand.NotACommand -> {
                        connection.client.sendMessage(channelName, message)
                    }
                    else -> {
                        // For commands, reuse existing logic by temporarily setting current channel
                        // if needed?
                        // Or just reimplement basic command handling for split view?
                        // Most commands (JOIN, PART, etc) are global or specific.
                        // Message, Action, etc can be handled directly.
                        if (command is IrcCommand.Message) {
                            connection.client.sendMessage(command.target, command.message)
                        } else if (command is IrcCommand.Action) {
                            connection.client.sendMessage(
                                    channelName,
                                    "\u0001ACTION ${command.action}\u0001"
                            )
                        } else if (command is IrcCommand.Raw) {
                            connection.client.sendRawCommand(command.command)
                        } else if (command is IrcCommand.Unknown) {
                            // Helper to add system message to specific channel
                            addSystemMessage(
                                    serverId,
                                    channelName,
                                    "Unknown command: /${command.command}"
                            )
                        } else {
                            // Fallback to strict processing or ignore complex commands for now in
                            // split view
                            // unless we fully Duplicate the parser logic.
                            // Ideally IrcCommandParser returns an object we can handle on a
                            // specific connection.
                            // The existing sendMessage() logic is tightly coupled to current
                            // channel.
                            // We should refactor the main sendMessage to use this new method!
                            // For now, let's just handle basic chat provided by the ChatPanel.
                            // ChatPanel input basically sends text.
                            if (message.startsWith("/")) {
                                // Delegate to existing logic if it's a command, but it might act on
                                // current channel?
                                // Commands like /join work on context.
                                // If I type /join #foo in split pane 2, it should assume server 2.
                                // But existing logic uses _currentServerId.
                                // I cannot easily reuse existing sendMessage for other servers
                                // without changing _currentServerId logic.
                                // So I will handle essential message types here.
                            } else {
                                connection.client.sendMessage(channelName, message)
                            }
                        }
                    }
                }
            }
        }
    }

    fun sendMessage(message: String) {
        // Reset AFK timer on user activity

        val serverId = _currentServerId.value ?: return
        val channelName = _currentChannel.value

        connections[serverId]?.let { connection ->
            managerScope.launch {
                when (val command = IrcCommandParser.parse(message)) {
                    is IrcCommand.NotACommand -> {
                        if (channelName != null) {
                            connection.client.sendMessage(channelName, message)
                        }
                    }
                    is IrcCommand.Join -> joinChannel(serverId, command.channel)
                    is IrcCommand.Part -> {
                        val target = command.channel ?: channelName
                        if (target != null) {
                            connection.client.sendRawCommand(
                                    "PART $target :${command.message ?: "Leaving"}"
                            )
                            updateSavedChannels(serverId, target, false)
                        }
                    }
                    is IrcCommand.Message ->
                            connection.client.sendMessage(command.target, command.message)
                    is IrcCommand.Action -> {
                        if (channelName != null) {
                            connection.client.sendMessage(
                                    channelName,
                                    "\u0001ACTION ${command.action}\u0001"
                            )
                        }
                    }
                    is IrcCommand.Nick ->
                            connection.client.sendRawCommand("NICK ${command.newNick}")
                    is IrcCommand.Whois ->
                            connection.client.sendRawCommand("WHOIS ${command.nickname}")
                    is IrcCommand.Quit -> {
                        connection.client.sendRawCommand("QUIT :${command.message ?: "Leaving"}")
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
                            addSystemMessage(
                                    serverId,
                                    channelName,
                                    "Ignored user: ${command.nickname}"
                            )
                        }
                    }
                    is IrcCommand.Unignore -> {
                        removeIgnoredUser(command.nickname)
                        if (channelName != null) {
                            addSystemMessage(
                                    serverId,
                                    channelName,
                                    "Unignored user: ${command.nickname}"
                            )
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
                            connection.client.sendRawCommand(
                                    "MODE $channelName +b ${command.nickname}!*@*"
                            )
                        }
                    }
                    is IrcCommand.Unban -> {
                        if (channelName != null) {
                            connection.client.sendRawCommand(
                                    "MODE $channelName -b ${command.nickname}!*@*"
                            )
                        }
                    }
                    is IrcCommand.Voice -> {
                        if (channelName != null) {
                            connection.client.sendRawCommand(
                                    "MODE $channelName +v ${command.nickname}"
                            )
                        }
                    }
                    is IrcCommand.Devoice -> {
                        if (channelName != null) {
                            connection.client.sendRawCommand(
                                    "MODE $channelName -v ${command.nickname}"
                            )
                        }
                    }
                    is IrcCommand.Op -> {
                        if (channelName != null) {
                            connection.client.sendRawCommand(
                                    "MODE $channelName +o ${command.nickname}"
                            )
                        }
                    }
                    is IrcCommand.Deop -> {
                        if (channelName != null) {
                            connection.client.sendRawCommand(
                                    "MODE $channelName -o ${command.nickname}"
                            )
                        }
                    }
                    is IrcCommand.Mode -> {
                        if (channelName != null) {
                            connection.client.sendRawCommand(
                                    "MODE $channelName ${command.modeString}"
                            )
                        }
                    }
                    is IrcCommand.Topic -> {
                        if (channelName != null) {
                            if (command.topic != null) {
                                connection.client.sendRawCommand(
                                        "TOPIC $channelName :${command.topic}"
                                )
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
                        connection.client.sendRawCommand(
                                "NAMES ${command.channel ?: channelName ?: ""}"
                        )
                    }
                    is IrcCommand.Notice -> {
                        connection.client.sendRawCommand(
                                "NOTICE ${command.target} :${command.message}"
                        )
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
                            managerScope.launch {
                                connection.client.partChannel(target)
                                delay(200) // Brief delay to ensure part is processed
                                connection.client.joinChannel(target)
                            }
                        }
                    }
                    is IrcCommand.Away -> {
                        connection.client.sendRawCommand(
                                if (command.message != null) "AWAY :${command.message}" else "AWAY"
                        )
                    }
                    is IrcCommand.Back -> connection.client.sendRawCommand("AWAY")
                    is IrcCommand.Who -> connection.client.sendRawCommand("WHO ${command.mask}")
                    is IrcCommand.Ping ->
                            connection.client.sendRawCommand("PING ${command.target ?: ""}")
                    is IrcCommand.Time ->
                            connection.client.sendRawCommand("TIME ${command.server ?: ""}")
                    is IrcCommand.Version ->
                            connection.client.sendRawCommand("VERSION ${command.server ?: ""}")
                    is IrcCommand.Motd ->
                            connection.client.sendRawCommand("MOTD ${command.server ?: ""}")
                    is IrcCommand.Info ->
                            connection.client.sendRawCommand("INFO ${command.server ?: ""}")
                    is IrcCommand.Unknown -> {
                        if (channelName != null) {
                            addSystemMessage(
                                    serverId,
                                    channelName,
                                    "Unknown command: /${command.command}"
                            )
                        }
                    }
                    else -> {}
                }
            }
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
                val currentChannels = connection.client.channels.value
                if (!currentChannels.containsKey(nickname)) {
                    val updatedChannels =
                            currentChannels +
                                    (nickname to Channel(name = nickname, type = ChannelType.QUERY))
                    connection.client.channels.value.plus(
                            nickname to Channel(name = nickname, type = ChannelType.QUERY)
                    )
                }
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
