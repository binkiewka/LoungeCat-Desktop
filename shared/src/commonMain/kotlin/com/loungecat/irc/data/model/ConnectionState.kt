package com.loungecat.irc.data.model

sealed class ConnectionState {
    data object Disconnected : ConnectionState()
    data object Connecting : ConnectionState()
    data class Connected(val serverName: String) : ConnectionState()
    data class Error(val message: String) : ConnectionState()
    data class Reconnecting(val attempt: Int) : ConnectionState()
}
