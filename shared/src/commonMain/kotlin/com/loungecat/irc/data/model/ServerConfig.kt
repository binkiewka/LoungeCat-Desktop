package com.loungecat.irc.data.model

data class ServerConfig(
        val id: Long = 0,
        val serverName: String,
        val hostname: String,
        val port: Int = 6697,
        val useSsl: Boolean = true,
        val acceptSelfSignedCerts: Boolean = false,
        val serverPassword: String? = null,
        val nickname: String,
        val altNickname: String = "",
        val username: String = "",
        val realName: String = "",
        val useSasl: Boolean = false,
        val saslUsername: String? = null,
        val saslPassword: String? = null,
        val autoConnect: Boolean = false,
        val autoJoinChannels: String = "",
        val onConnectCommands: String = "",
        // Batch 4: NickServ Auto-Identify
        val nickServPassword: String? = null,
        val nickServCommand: String =
                "IDENTIFY", // e.g., "IDENTIFY" or "IDENTIFY {nick} {password}"
        // Batch 4: SSL Fingerprint Verification
        val pinnedCertFingerprint: String? = null, // SHA-256 fingerprint
        val trustOnFirstUse: Boolean = false, // TOFU mode

        // Proxy Settings
        val proxyType: ProxyType = ProxyType.NONE,
        val proxyHost: String = "",
        val proxyPort: Int = 1080,
        val proxyUsername: String? = null,
        val proxyPassword: String? = null,

        // Server Type
        val type: ServerType = ServerType.IRC,
        val matrixHomeserver: String? = null,
        val matrixAccessToken: String? = null
)

enum class ProxyType {
        NONE,
        HTTP,
        SOCKS
}

enum class ServerType {
        IRC,
        MATRIX
}
