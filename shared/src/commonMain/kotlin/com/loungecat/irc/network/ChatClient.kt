package com.loungecat.irc.network

import com.loungecat.irc.data.model.Channel
import com.loungecat.irc.data.model.ConnectionState
import com.loungecat.irc.data.model.IncomingMessage
import com.loungecat.irc.data.model.ServerConfig
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow

interface ChatClient {
    val connectionState: StateFlow<ConnectionState>
    val messages: SharedFlow<IncomingMessage>
    val channels: StateFlow<Map<String, Channel>>
    val whoisCache: StateFlow<Map<String, String>>

    // Config access needed by manager
    val config: ServerConfig
    val serverId:
            Long // Ideally this belongs to a higher level wrapper, but keeping it here for now
    // eases migration

    suspend fun connect()
    suspend fun disconnect()

    suspend fun sendMessage(target: String, message: String)
    suspend fun sendRawCommand(command: String)

    suspend fun joinChannel(channelName: String)
    suspend fun partChannel(channelName: String)

    // Query window management (local state mostly)
    suspend fun startQuery(nickname: String)
    suspend fun closeQuery(nickname: String)

    fun getCurrentNickname(): String
    fun updateConfig(newConfig: ServerConfig)

    fun requestSilentWhois(nickname: String)
}
