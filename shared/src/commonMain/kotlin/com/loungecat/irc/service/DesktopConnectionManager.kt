package com.loungecat.irc.service

import com.loungecat.irc.data.model.*
import com.loungecat.irc.data.model.MessageType
import com.loungecat.irc.network.ChatClient
import com.loungecat.irc.network.IrcClient
import com.loungecat.irc.util.HighlightMatcher
import com.loungecat.irc.util.ImageUrlDetector
import com.loungecat.irc.util.IrcCommand
import com.loungecat.irc.util.IrcCommandParser
import com.loungecat.irc.util.Logger
import com.loungecat.irc.util.UrlExtractor
import com.loungecat.irc.util.UserActivityTracker
import com.loungecat.irc.util.getSystemInfo
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

class DesktopConnectionManager {

    private val connectionMutex = Mutex()

    private val exceptionHandler = CoroutineExceptionHandler { _, throwable ->
        Logger.e("DesktopConnectionManager", "Uncaught exception in coroutine", throwable)
    }

    private val managerScope = CoroutineScope(SupervisorJob() + Dispatchers.IO + exceptionHandler)

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

    // WHOIS cache: nickname (lowercase) -> formatted WHOIS info (aggregated from all servers)
    private val _whoisCache = MutableStateFlow<Map<String, String>>(emptyMap())
    val whoisCache: StateFlow<Map<String, String>> = _whoisCache.asStateFlow()

    private val _joinPartQuitMode = MutableStateFlow(JoinPartQuitDisplayMode.SHOW_ALL)
    val joinPartQuitMode: StateFlow<JoinPartQuitDisplayMode> = _joinPartQuitMode.asStateFlow()

    // User activity tracker for smart hide feature
    val userActivityTracker = UserActivityTracker()

    // User preferences state
    private val _userPreferences = MutableStateFlow(UserPreferences())
    val userPreferences: StateFlow<UserPreferences> = _userPreferences.asStateFlow()

    // Reconnect job tracking
    private val reconnectJobs = mutableMapOf<Long, Job>()

    // Connection listener jobs tracking
    private val connectionListenerJobs = mutableMapOf<Long, List<Job>>()

    // Connection stability jobs tracking (prevents rapid reconnect loops)
    private val connectionStabilityJobs = mutableMapOf<Long, Job>()

    // Message cache callback (set by platform-specific code)
    // Message cache callback (set by platform-specific code)
    // Message cache callback (set by platform-specific code)
    // declarations moved to lines 145+

    // Track loading state for scrollback
    private val _isLoadingOlderMessages = MutableStateFlow(false)
    val isLoadingOlderMessages: StateFlow<Boolean> = _isLoadingOlderMessages.asStateFlow()

    fun setJoinPartQuitMode(mode: JoinPartQuitDisplayMode) {
        _joinPartQuitMode.value = mode
        updatePreference { it.copy(joinPartQuitMode = mode) }
    }

    fun setInputHistorySize(size: Int) {
        updatePreference { it.copy(inputHistorySize = size) }
    }

    fun setLoggingEnabled(enabled: Boolean) {
        updatePreference { it.copy(loggingEnabled = enabled) }
    }

    fun setHistoryReplayLines(lines: Int) {
        updatePreference { it.copy(historyReplayLines = lines) }
    }

    fun setProcessLinkPreviewsFromOthers(enabled: Boolean) {
        updatePreference { it.copy(processLinkPreviewsFromOthers = enabled) }
    }

    fun setUseProxyForPreviews(enabled: Boolean) {
        updatePreference { it.copy(useProxyForPreviews = enabled) }
    }

    fun setSmartHideMinutes(minutes: Int) {
        updatePreference { it.copy(smartHideMinutes = minutes) }
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

    // Callbacks for MessageCache interaction
    var messageCacheLoader: ((Long, String) -> List<IncomingMessage>)? = null
    var messageCacheSaver: ((Long, String, List<IncomingMessage>, String?) -> Unit)? = null
    var messageCachePaginatedLoader: ((Long, String, Int, Int) -> List<IncomingMessage>)? = null
    var messageCacheCountLoader: ((Long, String) -> Int)? = null
    var topicLoader: ((Long, String) -> String?)? = null
    var textLogger: ((Long, String, String, IncomingMessage) -> Unit)? = null
    var textLogLoader: ((Long, String, String, Int) -> List<IncomingMessage>)? = null

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
        _joinPartQuitMode.value = prefs.joinPartQuitMode
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

    /**
     * Update server configuration without reconnecting. Saves the config to database and updates
     * in-memory state.
     */
    fun updateServerConfig(config: ServerConfig) {
        managerScope.launch {
            databaseService?.let { db ->
                db.updateServerConfig(config)
                Logger.d(
                        "DesktopConnectionManager",
                        "Updated server config (no reconnect): ${config.serverName}"
                )
            }

            // Update the in-memory connection config if it exists
            _connections.update { current ->
                val existing = current[config.id]
                if (existing != null) {
                    current + (config.id to existing.copy(config = config))
                } else {
                    current
                }
            }

            // Reload saved configs
            loadSavedServers()
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

                    val client: ChatClient =
                            if (configToUse.type == ServerType.MATRIX) {
                                com.loungecat.irc.network.MatrixClient(
                                        configToUse,
                                        configToUse.id,
                                        managerScope
                                )
                            } else {
                                IrcClient(configToUse, configToUse.id, managerScope)
                            }

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

                    // Cancel stability job
                    connectionStabilityJobs[serverId]?.cancel()
                    connectionStabilityJobs.remove(serverId)

                    val updatedConnection =
                            connection.copy(
                                    config = updatedConfig,
                                    connectionState = ConnectionState.Disconnected,
                                    channels = emptyList(),
                                    messages = emptyMap(),
                                    manuallyDisconnected = true,
                                    reconnectAttempt = 0 // Reset on manual disconnect
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
            val connection = connections[serverId]
            if (connection != null) {
                connect(connection.config)
            } else {
                Logger.d("DesktopConnectionManager", "No connection found for $serverId")
            }
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

                    // Cancel stability jobs
                    connectionStabilityJobs[serverId]?.cancel()
                    connectionStabilityJobs.remove(serverId)

                    updateServerList()
                }
            }
        }
    }

    fun disconnectAll() {
        managerScope.launch { connections.values.forEach { it.client.disconnect() } }
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
        val channelName = _currentChannel.value ?: ""

        managerScope.launch { processCommand(serverId, channelName, message) }
    }

    /** Request WHOIS info for a nickname (only if not already cached) */
    fun requestWhois(serverId: Long, nickname: String) {
        val nick = nickname.lowercase()
        // Skip if already cached
        if (_whoisCache.value.containsKey(nick)) return

        managerScope.launch { connections[serverId]?.client?.sendRawCommand("WHOIS $nickname") }
    }

    fun requestSilentWhois(serverId: Long, nickname: String) {
        // We DON'T skip if cached, because a manual click usually implies "refresh" or "show me".
        // The dialog will use the cache but triggering the request ensures fresh data.
        managerScope.launch { connections[serverId]?.client?.requestSilentWhois(nickname) }
    }

    // --- Message Processing & Sending ---
    //

    suspend fun uploadImage(fileBytes: ByteArray, filename: String? = null): String? {
        val apiKey = _userPreferences.value.imgbbApiKey
        if (apiKey.isNullOrBlank()) {
            Logger.e("DesktopConnectionManager", "ImgBB API Key not set")
            // Ideally notify user via a system message or error event?
            // For now, we rely on the UI to check or handle the null return.
            return null
        }
        return ImgBbService.uploadImage(apiKey, fileBytes, filename)
    }

    private suspend fun processCommand(serverId: Long, target: String, message: String) {
        val connection = connections[serverId] ?: return

        // Check for URL Shortening
        var finalMessage = message
        val prefs = _userPreferences.value
        if (prefs.urlShorteningEnabled) {
            val urls = UrlExtractor.extractUrls(message)
            var processedMessage = message
            urls.forEach { url ->
                if (url.length > prefs.urlShorteningThreshold) {
                    val shortened = UrlShortenerService.shortenUrl(url)
                    if (shortened != url) {
                        processedMessage = processedMessage.replace(url, shortened)
                    }
                }
            }
            finalMessage = processedMessage
        }

        when (val command = IrcCommandParser.parse(finalMessage)) {
            is IrcCommand.NotACommand -> {
                sendMessageOrUsePastebin(connection, target, finalMessage)
            }
            is IrcCommand.Join -> joinChannel(serverId, command.channel)
            is IrcCommand.Part -> {
                val partTarget = command.channel ?: target
                if (partTarget.startsWith("#") ||
                                partTarget.startsWith("&") ||
                                partTarget.startsWith("+") ||
                                partTarget.startsWith("!")
                ) {
                    connection.client.sendRawCommand(
                            "PART $partTarget :${command.message ?: "Leaving"}"
                    )
                    updateSavedChannels(serverId, partTarget, false)
                } else {
                    // Close query locally
                    connection.client.closeQuery(partTarget)
                }
            }
            is IrcCommand.Message ->
                    sendMessageOrUsePastebin(connection, command.target, command.message)
            is IrcCommand.Action -> {
                connection.client.sendMessage(target, "\u0001ACTION ${command.action}\u0001")
            }
            is IrcCommand.Nick -> connection.client.sendRawCommand("NICK ${command.newNick}")
            is IrcCommand.Identify -> {
                // Ensure NickServ window is open so the reply goes there
                startPrivateMessage("NickServ")

                val msg = "IDENTIFY ${command.args}"
                connection.client.sendRawCommand("PRIVMSG NickServ :$msg")
                addSystemMessage(serverId, target, "Sent identification to NickServ")
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
                clearChannelHistory(serverId, target)
            }
            is IrcCommand.Help -> {
                addSystemMessage(serverId, target, IrcCommandParser.getHelpText())
            }
            is IrcCommand.SysInfo -> {
                val args = command.args
                val isPublic = args.contains("-o") || args.contains("--public")
                val flags = setOf("-o", "--public", "-e", "--echo")
                val subcommands = args.filter { !flags.contains(it) }.map { it.lowercase() }

                val showAll =
                        subcommands.isEmpty() ||
                                subcommands.contains("full") ||
                                subcommands.contains("all")

                val sysInfo =
                        getSystemInfo(
                                hideOs = !showAll && !subcommands.contains("os"),
                                hideCpu = !showAll && !subcommands.contains("cpu"),
                                hideMemory =
                                        !showAll &&
                                                !subcommands.contains("memory") &&
                                                !subcommands.contains("mem"),
                                hideStorage =
                                        !showAll &&
                                                !subcommands.contains("storage") &&
                                                !subcommands.contains("disk"),
                                hideVga =
                                        !showAll &&
                                                !subcommands.contains("vga") &&
                                                !subcommands.contains("gpu"),
                                hideUptime = !showAll && !subcommands.contains("uptime")
                        )

                if (isPublic) {
                    sendMessageOrUsePastebin(connection, target, sysInfo)
                } else {
                    addSystemMessage(serverId, target, sysInfo)
                }
            }
            is IrcCommand.Ignore -> {
                ignoreUser(command.nickname)
                addSystemMessage(serverId, target, "Ignored user: ${command.nickname}")
            }
            is IrcCommand.Unignore -> {
                removeIgnoredUser(command.nickname)
                addSystemMessage(serverId, target, "Unignored user: ${command.nickname}")
            }
            is IrcCommand.Kick -> {
                val kickCmd =
                        if (command.reason != null) {
                            "KICK $target ${command.nickname} :${command.reason}"
                        } else {
                            "KICK $target ${command.nickname}"
                        }
                connection.client.sendRawCommand(kickCmd)
            }
            is IrcCommand.Ban -> {
                connection.client.sendRawCommand("MODE $target +b ${command.nickname}!*@*")
            }
            is IrcCommand.Unban -> {
                connection.client.sendRawCommand("MODE $target -b ${command.nickname}!*@*")
            }
            is IrcCommand.Voice -> {
                connection.client.sendRawCommand("MODE $target +v ${command.nickname}")
            }
            is IrcCommand.Devoice -> {
                connection.client.sendRawCommand("MODE $target -v ${command.nickname}")
            }
            is IrcCommand.Op -> {
                connection.client.sendRawCommand("MODE $target +o ${command.nickname}")
            }
            is IrcCommand.Deop -> {
                connection.client.sendRawCommand("MODE $target -o ${command.nickname}")
            }
            is IrcCommand.Mode -> {
                connection.client.sendRawCommand("MODE $target ${command.modeString}")
            }
            is IrcCommand.Topic -> {
                if (command.topic != null) {
                    connection.client.sendRawCommand("TOPIC $target :${command.topic}")
                } else {
                    connection.client.sendRawCommand("TOPIC $target")
                }
            }
            is IrcCommand.Invite -> {
                connection.client.sendRawCommand(
                        "INVITE ${command.nickname} ${command.channel ?: target}"
                )
            }
            is IrcCommand.List -> {
                connection.client.sendRawCommand(
                        if (command.filter != null) "LIST ${command.filter}" else "LIST"
                )
            }
            is IrcCommand.Names -> {
                connection.client.sendRawCommand("NAMES ${command.channel ?: target}")
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
                val cycleTarget = command.channel ?: target
                connection.client.partChannel(cycleTarget)
                delay(200) // Brief delay to ensure part is processed
                connection.client.joinChannel(cycleTarget)
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
                startPrivateMessage("NickServ")
                if (command.args.isNotBlank()) {
                    sendMessage(serverId, "NickServ", command.args)
                } else {
                    sendMessage(serverId, "NickServ", "HELP")
                }
            }
            is IrcCommand.ChanServ -> {
                startPrivateMessage("ChanServ")
                if (command.args.isNotBlank()) {
                    sendMessage(serverId, "ChanServ", command.args)
                } else {
                    sendMessage(serverId, "ChanServ", "HELP")
                }
            }
            is IrcCommand.MemoServ -> {
                startPrivateMessage("MemoServ")
                if (command.args.isNotBlank()) {
                    sendMessage(serverId, "MemoServ", command.args)
                } else {
                    sendMessage(serverId, "MemoServ", "HELP")
                }
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
                connection.client.sendRawCommand("MODE $target +b ${command.nickname}!*@*")
                val kickReason = command.reason ?: "Requested"
                connection.client.sendRawCommand("KICK $target ${command.nickname} :$kickReason")
            }
            is IrcCommand.Ctcp -> {
                // CTCP is just a PRIVMSG with \u0001 wrapping
                connection.client.sendMessage(command.target, "\u0001${command.message}\u0001")
            }
            is IrcCommand.Unknown -> {
                addSystemMessage(serverId, target, "Unknown command: /${command.command}")
            }
            else -> {}
        }
    }

    private suspend fun sendMessageOrUsePastebin(
            connection: ServerConnection,
            target: String,
            message: String
    ) {
        val threshold = _userPreferences.value.pastebinThreshold
        if (PastebinService.shouldPaste(message, maxLength = threshold)) {
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

        _connections.update { currentConnections ->
            val connection = currentConnections[serverId] ?: return@update currentConnections

            // Try to load connected messages if we don't have any for this channel
            val currentChannelMessages = connection.messages[channelName]
            val updatedMessages =
                    if (currentChannelMessages.isNullOrEmpty()) {
                        // Try to load from cache
                        val limit = _userPreferences.value.historyReplayLines
                        val cachedMessages =
                                messageCacheLoader?.invoke(serverId, channelName) ?: emptyList()

                        // Fallback/Augment from text logs if cache is insufficient
                        val messagesToUse =
                                if (cachedMessages.size < limit) {
                                    val logMessages =
                                            textLogLoader?.invoke(
                                                    serverId,
                                                    connection.config.serverName,
                                                    channelName,
                                                    limit
                                            )
                                                    ?: emptyList()

                                    if (logMessages.isNotEmpty()) {
                                        Logger.d(
                                                "DesktopConnectionManager",
                                                "Loaded ${logMessages.size} messages from TEXT LOGS for $channelName"
                                        )
                                    }

                                    // Merge and deduplicate
                                    // We prioritize cached messages as they might have more
                                    // metadata
                                    // Deduplication strategy: strict timestamp + content + sender
                                    // match
                                    val allMessages = cachedMessages + logMessages
                                    val uniqueMessages =
                                            allMessages
                                                    .distinctBy {
                                                        "${it.timestamp / 1000}-${it.sender}-${it.content}"
                                                    }
                                                    .sortedBy { it.timestamp }

                                    uniqueMessages
                                } else {
                                    cachedMessages
                                }

                        // We might want to respect the limit here immediately for display
                        val displayedMessages =
                                if (messagesToUse.size > limit) {
                                    messagesToUse.takeLast(limit)
                                } else {
                                    messagesToUse
                                }

                        if (displayedMessages.isNotEmpty()) {
                            Logger.d(
                                    "DesktopConnectionManager",
                                    "Loaded ${displayedMessages.size} messages (cache/logs) for $channelName"
                            )
                            // Process URLs for previews in cached messages
                            displayedMessages.forEach { msg ->
                                if (msg.type == MessageType.NORMAL) {
                                    processMessageUrls(serverId, msg)
                                }
                            }
                            connection.messages + (channelName to displayedMessages)
                        } else {
                            connection.messages
                        }
                    } else {
                        connection.messages
                    }

            val updatedConnection =
                    connection.copy(currentChannel = channelName, messages = updatedMessages)

            // Side effects (updating other observables) - risky inside update block if they trigger
            // other updates
            // but these are just StateFlow value sets, so it's fine.
            // Ideally should be done after update, but we need the new data.
            // We'll do it after the update block using the result.
            currentConnections + (serverId to updatedConnection)
        }

        // Update derived state
        connections[serverId]?.let { updatedConnection ->
            _currentChannel.value = channelName
            _messages.value = updatedConnection.messages
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

            // Process URLs for previews in older messages
            olderMessages.forEach { msg ->
                if (msg.type == MessageType.NORMAL) {
                    processMessageUrls(serverId, msg)
                }
            }

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
        // Cancel all listener jobs
        connectionListenerJobs.values.flatten().forEach { it.cancel() }
        connectionListenerJobs.clear()

        // Cancel all stability jobs
        connectionStabilityJobs.values.forEach { it.cancel() }
        connectionStabilityJobs.clear()

        disconnectAll()
        managerScope.cancel()
    }

    private fun setupClientListeners(serverId: Long, client: ChatClient): List<Job> {
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

        // Collect WHOIS cache updates from this client
        jobs.add(
                managerScope.launch {
                    client.whoisCache.collect { whoisData ->
                        // Merge with global cache
                        _whoisCache.update { current -> current + whoisData }
                    }
                }
        )

        return jobs
    }

    private fun updateConnectionState(serverId: Long, state: ConnectionState) {
        // Logger.d("DesktopConnectionManager", "DEBUG: updateConnectionState server=$serverId
        // state=$state")

        _connections.update { currentConnections ->
            val connection = currentConnections[serverId] ?: return@update currentConnections

            val updatedConnection =
                    when (state) {
                        is ConnectionState.Connected -> {
                            // Reset reconnect attempts ONLY after stability check
                            reconnectJobs[serverId]?.cancel()
                            reconnectJobs.remove(serverId)

                            // Cancel any existing stability job
                            connectionStabilityJobs[serverId]?.cancel()

                            // Start new stability job
                            connectionStabilityJobs[serverId] =
                                    managerScope.launch {
                                        delay(120_000) // 2 minutes stability threshold
                                        _connections.update { current ->
                                            val conn = current[serverId] ?: return@update current
                                            if (conn.isConnected()) {
                                                Logger.d(
                                                        "DesktopConnectionManager",
                                                        "Connection stable for server $serverId, resetting reconnect attempts"
                                                )
                                                current +
                                                        (serverId to
                                                                conn.copy(reconnectAttempt = 0))
                                            } else current
                                        }
                                    }

                            // Don't reset reconnectAttempt yet
                            connection.copy(connectionState = state)
                        }
                        is ConnectionState.Disconnected, is ConnectionState.Error -> {
                            // Cancel stability job - we failed to stay connected
                            connectionStabilityJobs[serverId]?.cancel()
                            connectionStabilityJobs.remove(serverId)

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
            currentConnections + (serverId to updatedConnection)
        }

        if (_currentServerId.value == serverId) {
            _connectionState.value = state
        }

        updateServerList()
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
        val newChannelList = channels.values.toList()

        // Debug logging for user list sync
        Logger.d(
                "DesktopConnectionManager",
                "updateChannels for server $serverId. Channel count: ${newChannelList.size}"
        )

        _connections.update { currentConnections ->
            val connection = currentConnections[serverId] ?: return@update currentConnections

            // Create a map of existing channels for state preservation
            val existingChannelsMap = connection.channels.associateBy { it.name }

            val mergedChannelList =
                    newChannelList.map { newChannel ->
                        val existingChannel = existingChannelsMap[newChannel.name]
                        if (existingChannel != null) {
                            // Preserve local state from existing channel
                            newChannel.copy(
                                    unreadCount = existingChannel.unreadCount,
                                    hasUnread = existingChannel.hasUnread,
                                    hasRecentActivity = existingChannel.hasRecentActivity,
                                    lastActivityTimestamp = existingChannel.lastActivityTimestamp,
                                    displayName = existingChannel.displayName
                            )
                        } else {
                            newChannel
                        }
                    }

            val updatedConnection = connection.copy(channels = mergedChannelList)
            currentConnections + (serverId to updatedConnection)
        }

        // We need to fetch the updated list from the connections map to ensure consistency
        // or just use our calculated merged list. Using the calculated list is safe here.
        // However, we need to be careful about race conditions if _connections was updated
        // elsewhere in parallel? _connections.update is atomic for the map, but we are inside the
        // update block above.
        // To be safe and clean, let's re-read the result or just use the logic that we know
        // applies.

        // Actually, we can just grab the result from the update above? No, update returns the new
        // map.
        // Let's just assume our merged list is correct for the UI update.

        // Wait, since we are inside `update`, we can't easily extract the result out.
        // But we computed `mergedChannelList`.
        // Let's re-read from _connections to be 100% sure we sync with what was just saved.

        val updatedChannels = _connections.value[serverId]?.channels ?: emptyList()

        if (_currentServerId.value == serverId) {
            _channels.value = updatedChannels
        }

        updateServerList()
    }

    private fun updateMessages(serverId: Long, message: IncomingMessage) {
        if (!message.isSelf && ignoredUsers.contains(message.sender.lowercase())) {
            Logger.d("DesktopConnectionManager", "Ignoring message from ${message.sender}")
            return
        }

        // Topic Deduplication
        if (message.type == MessageType.TOPIC) {
            val cachedTopic = topicLoader?.invoke(serverId, message.target)
            val newTopic = message.content.removePrefix("Topic: ")
            // Simple extraction, depends on how IrcClient formats "Topic: "
            // The IrcClient sends "Topic: ${topic}" so we check containment or exact match if
            // possible
            // But simpler: just save it. The logic to NOT show it requires filtering.

            // Wait, if it's identical to cached, we return.
            // We need to parse strict content.
            // In IrcClient: content = "Topic: ${topic ?: "(no topic)"}"
            val actualTopicContent =
                    if (message.content.startsWith("Topic: ")) {
                        message.content.substring(7)
                    } else {
                        message.content
                    }

            if (cachedTopic == actualTopicContent) {
                Logger.d("DesktopConnectionManager", "Deduplicated topic for ${message.target}")
                return
            }
        }

        _connections.update { currentConnections ->
            val connection = currentConnections[serverId] ?: return@update currentConnections

            val updatedMessages = connection.messages.toMutableMap()
            val targetMessages =
                    updatedMessages.getOrDefault(message.target, emptyList()).toMutableList()
            targetMessages.add(message)

            if (targetMessages.size > 2000) {
                targetMessages.removeAt(0)
            }

            updatedMessages[message.target] = targetMessages

            val updatedConnection = connection.copy(messages = updatedMessages)

            // Side effect: Save to cache (safe to call here or outside, but we need targetMessages)
            // If it's a topic update, pass the topic
            val topicToSave =
                    if (message.type == MessageType.TOPIC) {
                        if (message.content.startsWith("Topic: ")) message.content.substring(7)
                        else message.content
                    } else null

            messageCacheSaver?.invoke(serverId, message.target, targetMessages, topicToSave)

            currentConnections + (serverId to updatedConnection)
        }

        // Update Unread Counts
        // We do this separately to ensure we are modifying the 'latest' connection state
        // and because it modifies 'channels', not 'messages'.
        if (!message.isSelf && message.type == MessageType.NORMAL) {
            _connections.update { currentConnections ->
                val connection = currentConnections[serverId] ?: return@update currentConnections
                val currentChannels = connection.channels.toMutableList()
                val index = currentChannels.indexOfFirst { it.name == message.target }

                if (index != -1) {
                    val channel = currentChannels[index]
                    currentChannels[index] =
                            channel.copy(unreadCount = channel.unreadCount + 1, hasUnread = true)
                }
                currentConnections + (serverId to connection.copy(channels = currentChannels))
            }
            updateServerList()
        }

        // Note: We might be doing double updates to _connections here.
        // Ideally we'd merge them but readability first.

        // Update active view if needed
        if (_currentServerId.value == serverId) {
            // We can't easily get the just-updated connection here without another map lookup
            // but since we just updated it, we can re-fetch
            connections[serverId]?.let {
                _messages.value = it.messages
                _channels.value = it.channels
            }
        }

        if (message.type == MessageType.NORMAL || message.type == MessageType.ACTION) {
            // Track user activity for smart hide feature
            if (!message.isSelf) {
                userActivityTracker.recordActivity(message.target, message.sender)
            }
            if (message.type == MessageType.NORMAL) {
                processMessageUrls(serverId, message)
            }
        }

        // Text Logging
        if (_userPreferences.value.loggingEnabled) {
            connections[serverId]?.let { conn ->
                textLogger?.invoke(serverId, conn.config.serverName, message.target, message)
            }
        }

        // Check for mentions and highlights
        if (!message.isSelf && message.type == MessageType.NORMAL) {
            connections[serverId]?.let { currentConnection ->
                val nickname = currentConnection.config.nickname
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

    private fun getProxyForServer(serverId: Long): java.net.Proxy? {
        val config = connections[serverId]?.config ?: return null
        if (config.proxyType == ProxyType.NONE) return null

        return try {
            val type =
                    when (config.proxyType) {
                        ProxyType.HTTP -> java.net.Proxy.Type.HTTP
                        ProxyType.SOCKS -> java.net.Proxy.Type.SOCKS
                        else -> java.net.Proxy.Type.DIRECT
                    }
            java.net.Proxy(type, java.net.InetSocketAddress(config.proxyHost, config.proxyPort))
        } catch (e: Exception) {
            Logger.error(
                    "DesktopConnectionManager",
                    "Failed to create proxy for server $serverId",
                    e
            )
            null
        }
    }

    private fun processMessageUrls(serverId: Long, message: IncomingMessage) {
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

        // CHECK SECURITY POLICY
        val prefs = _userPreferences.value
        val isTrusted = message.sender.lowercase() in prefs.trustedPreviewUsers
        val shouldAutoFetch = message.isSelf || prefs.processLinkPreviewsFromOthers || isTrusted

        if (detectedImages.isNotEmpty() && shouldAutoFetch) {
            _imageUrls.update { current -> current + (message.id to detectedImages) }
        }

        if (!shouldAutoFetch) {
            // Do not fetch previews. The UI will see the URL but no preview data,
            // and should display a "Load Preview" button.
            return
        }

        fetchPreviewsForUrls(message.id, urlsToPreview, serverId)
    }

    fun fetchPreviewForMessage(messageId: String) {
        // Find the server and message for this ID
        var foundServerId: Long? = null
        var foundMessage: IncomingMessage? = null

        // Efficiently search
        for ((sId, conn) in connections) {
            // We need to search effectively.
            // Usually this is called from UI for visible message.
            // We can search recent history.
            // Assuming message is in cache.
            // Accessing messages map safely?
            // connections is ConcurrentHashMap.
            val msgList = conn.messages.values.flatten() // Expensive?
            // Just iterating channels might be better
            for (channelMessages in conn.messages.values) {
                val msg = channelMessages.find { it.id == messageId }
                if (msg != null) {
                    foundServerId = sId
                    foundMessage = msg
                    break
                }
            }
            if (foundMessage != null) break
        }

        // Check current server if fail (fallback)
        if (foundMessage == null) {
            return
        }

        val urls = UrlExtractor.extractUrls(foundMessage.content)
        // Filter out direct images if they are already handled by image loader,
        // BUT the user clicked "Load Preview" so maybe they want the metadata card for the image
        // too?
        // Or if it failed to load?
        // Actually, for "Click-to-Load", we want to force load everything that was skipped.
        // If it's an image, we want to add it to _imageUrls.
        // If it's a link, we want to add to _urlPreviews.

        Logger.d("DesktopConnectionManager", "Found message. URLs: ${urls.size}")

        val detectedImages = mutableListOf<String>()
        val urlsToPreview = mutableListOf<String>()

        urls.forEach { url ->
            if (ImageUrlDetector.isImageUrl(url)) detectedImages.add(url)
            else urlsToPreview.add(url)
        }

        if (detectedImages.isNotEmpty()) {
            _imageUrls.update { current -> current + (messageId to detectedImages) }
        }

        if (urlsToPreview.isNotEmpty()) {
            fetchPreviewsForUrls(messageId, urlsToPreview, foundServerId)
        }
        // This means images are Auto-Loaded regardless of preference?
        // Coil loads them.
        // THIS IS A SECURITY FLAV.
        // I need to gate Image adding too!

        // Returning to this task:
        // Update fetchPreviewForMessage to handle finding message and fetching.

        // Returning to this task:
        // Update fetchPreviewForMessage to handle finding message and fetching.
    }

    private fun fetchPreviewsForUrls(
            messageId: String,
            urls: List<String>,
            serverId: Long? = null
    ) {
        // If serverId is not provided, try to find it from the message ID (optimistic check)
        // effectively done by caller usually.

        // Determined proxy
        val proxy =
                if (serverId != null && _userPreferences.value.useProxyForPreviews) {
                    getProxyForServer(serverId)
                } else {
                    // If we are currently connected to a server, try to use its proxy as a
                    // fallback?
                    // Or better: ensure we always pass serverId.
                    // For auto-fetch (processMessageUrls), we are inside the context of a specific
                    // server/connection update loop?
                    // Actually processMessageUrls is called from updateMessages which has serverId.
                    // Wait, processMessageUrls currently doesn't take serverId. I need to update
                    // signature or logic.
                    null
                }

        urls.forEach { url ->
            managerScope.launch {
                val preview = urlPreviewService.fetchPreview(url, proxy)
                _urlPreviews.update { current ->
                    val existingPreviews = current[messageId] ?: emptyList()
                    // Avoid duplicates
                    if (existingPreviews.any { it.url == url }) return@update current

                    val newPreviews = existingPreviews + preview
                    current + (messageId to newPreviews)
                }
            }
        }
    }

    // Adjusted processMessageUrls to use internal lookup or scope if possible.
    // Since updateMessages has serverId, we should pass it down.

    fun trustUserForPreviews(nickname: String) {
        val lower = nickname.lowercase()
        updatePreference { it.copy(trustedPreviewUsers = it.trustedPreviewUsers + lower) }
    }

    fun untrustUserForPreviews(nickname: String) {
        val lower = nickname.lowercase()
        updatePreference { it.copy(trustedPreviewUsers = it.trustedPreviewUsers - lower) }
    }

    fun setImgbbApiKey(apiKey: String) {
        updatePreference { it.copy(imgbbApiKey = apiKey) }
    }

    fun setUrlShorteningEnabled(enabled: Boolean) {
        updatePreference { it.copy(urlShorteningEnabled = enabled) }
    }

    fun setUrlShorteningThreshold(threshold: Int) {
        updatePreference { it.copy(urlShorteningThreshold = threshold) }
    }

    fun markAsRead(serverId: Long, channelName: String) {
        _connections.update { currentConnections ->
            val connection = currentConnections[serverId] ?: return@update currentConnections
            val currentChannels = connection.channels.toMutableList()
            val index = currentChannels.indexOfFirst { it.name == channelName }

            if (index != -1) {
                val channel = currentChannels[index]
                if (channel.unreadCount > 0 || channel.hasUnread) {
                    currentChannels[index] = channel.copy(unreadCount = 0, hasUnread = false)
                }
            }
            currentConnections + (serverId to connection.copy(channels = currentChannels))
        }

        updateServerList()

        if (_currentServerId.value == serverId) {
            connections[serverId]?.let { _channels.value = it.channels }
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
        // Take a snapshot of values to avoid concurrent mod issues during map
        val currentConnections = connections.values.toList()

        val newServerList =
                currentConnections.map { connection ->
                    ServerListItem(
                            serverId = connection.serverId,
                            serverName = connection.config.serverName,
                            hostname = connection.config.hostname,
                            isConnected = connection.isConnected(),
                            unreadCount = connection.getUnreadCount(),
                            channels = connection.channels
                    )
                }
        _servers.value = newServerList
    }

    fun setPastebinThreshold(threshold: Int) {
        updatePreference { it.copy(pastebinThreshold = threshold) }
    }
}

data class ServerConnection(
        val serverId: Long,
        val config: ServerConfig,
        val client: ChatClient,
        val connectionState: ConnectionState = ConnectionState.Disconnected,
        val channels: List<Channel> = emptyList(),
        val messages: Map<String, List<IncomingMessage>> = emptyMap(),
        val currentChannel: String? = null,
        val manuallyDisconnected: Boolean = false,
        val reconnectAttempt: Int = 0
) {
    fun isConnected(): Boolean {
        return connectionState is ConnectionState.Connected
    }
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
