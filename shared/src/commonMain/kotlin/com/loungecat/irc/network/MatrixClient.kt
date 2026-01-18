package com.loungecat.irc.network

import com.loungecat.irc.data.model.Channel
import com.loungecat.irc.data.model.ChannelType
import com.loungecat.irc.data.model.ConnectionState
import com.loungecat.irc.data.model.IncomingMessage
import com.loungecat.irc.data.model.ServerConfig
import io.ktor.http.Url
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.launchIn
import kotlinx.coroutines.flow.onEach
import kotlinx.coroutines.launch
import net.folivo.trixnity.client.MatrixClient
import net.folivo.trixnity.client.login
import net.folivo.trixnity.client.media.InMemoryMediaStore
import net.folivo.trixnity.client.room
import net.folivo.trixnity.client.room.message.text
import net.folivo.trixnity.client.store.repository.createInMemoryRepositoriesModule
import net.folivo.trixnity.clientserverapi.model.authentication.IdentifierType
import net.folivo.trixnity.core.model.RoomAliasId
import net.folivo.trixnity.core.model.RoomId

class MatrixClient(
        override var config: ServerConfig,
        override val serverId: Long,
        private val scope: CoroutineScope
) : ChatClient {

    private var client: MatrixClient? = null
    private var syncJob: Job? = null

    private val _connectionState = MutableStateFlow<ConnectionState>(ConnectionState.Disconnected)
    override val connectionState: StateFlow<ConnectionState> = _connectionState.asStateFlow()

    private val _messages = MutableSharedFlow<IncomingMessage>()
    override val messages: SharedFlow<IncomingMessage> = _messages.asSharedFlow()

    private val _channels = MutableStateFlow<Map<String, Channel>>(emptyMap())
    override val channels: StateFlow<Map<String, Channel>> = _channels.asStateFlow()

    private val _whoisCache = MutableStateFlow<Map<String, String>>(emptyMap())
    override val whoisCache: StateFlow<Map<String, String>> = _whoisCache.asStateFlow()

    override suspend fun connect() {
        if (_connectionState.value is ConnectionState.Connected) return
        _connectionState.value = ConnectionState.Connecting

        try {
            var homeserver =
                    config.matrixHomeserver
                            ?: throw IllegalArgumentException("Missing Homeserver URL")
            if (!homeserver.startsWith("http://") && !homeserver.startsWith("https://")) {
                homeserver = "https://$homeserver"
            }
            // Use password from matrixAccessToken field (we'll rename in UI later)
            val password =
                    config.matrixAccessToken ?: throw IllegalArgumentException("Missing Password")
            val username = config.username ?: throw IllegalArgumentException("Missing Username")

            // Initialize Trixnity Client with password-based login
            val matrixClient =
                    MatrixClient.login(
                                    baseUrl = Url(homeserver),
                                    identifier = IdentifierType.User(username),
                                    password = password,
                                    repositoriesModule = createInMemoryRepositoriesModule(),
                                    mediaStore = InMemoryMediaStore()
                            )
                            .getOrThrow()

            this.client = matrixClient

            // Start Syncing
            syncJob = scope.launch { matrixClient.startSync() }

            // Listen for connection state changes using syncState
            matrixClient
                    .syncState
                    .onEach { state ->
                        // Map Trixnity's SyncState to our ConnectionState using enum name
                        val newState =
                                when (state.name) {
                                    "RUNNING" -> ConnectionState.Connected(config.serverName)
                                    "STARTED", "INITIAL_SYNC" -> ConnectionState.Connecting
                                    "STOPPED" -> ConnectionState.Disconnected
                                    "ERROR" -> ConnectionState.Error("Sync Error")
                                    else -> ConnectionState.Disconnected
                                }
                        _connectionState.value = newState
                    }
                    .launchIn(scope)

            // Set connected immediately after successful login
            _connectionState.value = ConnectionState.Connected(config.serverName)

            // Populate Channels from joined rooms
            // Using RoomService - getAll() returns Flow<Map<RoomId, Room>>
            scope.launch {
                try {
                    matrixClient
                            .room
                            .getAll()
                            .onEach { roomsMap ->
                                val channelMap = mutableMapOf<String, Channel>()
                                roomsMap.forEach { (roomId, _) ->
                                    // RoomId.toString() gives the full ID like !abc:matrix.org
                                    val roomIdStr = roomId.toString()
                                    // Use roomId as display name for now
                                    channelMap[roomIdStr] =
                                            Channel(
                                                    name = roomIdStr,
                                                    displayName = roomIdStr,
                                                    type = ChannelType.CHANNEL,
                                                    topic = "",
                                                    users = emptyList()
                                            )
                                }
                                _channels.value = channelMap
                            }
                            .launchIn(scope)
                } catch (e: Exception) {
                    e.printStackTrace()
                }
            }
        } catch (e: Exception) {
            e.printStackTrace()
            _connectionState.value =
                    ConnectionState.Error(e.message ?: "Failed to connect to Matrix")
        }
    }

    override suspend fun disconnect() {
        try {
            client?.stopSync()
            client?.close()
            syncJob?.cancel()
            client = null
        } catch (e: Exception) {
            // Ignore close errors
        } finally {
            _connectionState.value = ConnectionState.Disconnected
        }
    }

    override suspend fun sendMessage(target: String, message: String) {
        val client = client ?: return
        try {
            val roomId = RoomId(target)
            client.room.sendMessage(roomId) { text(message) }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    override suspend fun sendRawCommand(command: String) {
        // Matrix doesn't support raw commands like IRC
    }

    override suspend fun joinChannel(channelName: String) {
        val client = client ?: return
        try {
            if (channelName.startsWith("#")) {
                val alias = RoomAliasId(channelName)
                client.api.room.joinRoom(alias).getOrThrow()
            } else {
                val roomId = RoomId(channelName)
                client.api.room.joinRoom(roomId).getOrThrow()
            }
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    override suspend fun partChannel(channelName: String) {
        val client = client ?: return
        try {
            val roomId = RoomId(channelName)
            client.api.room.leaveRoom(roomId).getOrThrow()
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }

    override suspend fun startQuery(nickname: String) {
        // Matrix DMs are handled differently - would need to create/find a DM room
    }

    override suspend fun closeQuery(nickname: String) {
        // Leave the DM room
    }

    override fun getCurrentNickname(): String {
        return config.nickname
    }

    override fun updateConfig(newConfig: ServerConfig) {
        this.config = newConfig
    }
}
