package com.loungecat.irc.util

import com.loungecat.irc.data.model.ServerConfig
import java.text.SimpleDateFormat
import java.util.*
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

/**
 * Utility for exporting and importing server configurations. Note: Passwords are NEVER exported for
 * security reasons.
 */
object ConfigExporter {

    private val json = Json {
        prettyPrint = true
        encodeDefaults = true
        ignoreUnknownKeys = true
    }

    /** Exportable server config - passwords stripped for security. */
    @Serializable
    data class ExportableServerConfig(
            val serverName: String,
            val hostname: String,
            val port: Int,
            val useSsl: Boolean,
            val acceptSelfSignedCerts: Boolean,
            val nickname: String,
            val altNickname: String,
            val username: String,
            val realName: String,
            val useSasl: Boolean,
            val saslUsername: String?,
            val autoConnect: Boolean,
            val autoJoinChannels: String,
            val onConnectCommands: String,
            val nickServCommand: String,
            val trustOnFirstUse: Boolean
    // Note: Passwords intentionally excluded
    )

    @Serializable
    data class ExportData(
            val version: Int = 1,
            val exportedAt: String,
            val appVersion: String = "1.0.0",
            val servers: List<ExportableServerConfig>
    )

    /** Export server configurations to JSON. Passwords are stripped for security. */
    fun export(configs: List<ServerConfig>): String {
        val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault())

        val exportableConfigs =
                configs.map { config ->
                    ExportableServerConfig(
                            serverName = config.serverName,
                            hostname = config.hostname,
                            port = config.port,
                            useSsl = config.useSsl,
                            acceptSelfSignedCerts = config.acceptSelfSignedCerts,
                            nickname = config.nickname,
                            altNickname = config.altNickname,
                            username = config.username,
                            realName = config.realName,
                            useSasl = config.useSasl,
                            saslUsername = config.saslUsername,
                            autoConnect = config.autoConnect,
                            autoJoinChannels = config.autoJoinChannels,
                            onConnectCommands = config.onConnectCommands,
                            nickServCommand = config.nickServCommand,
                            trustOnFirstUse = config.trustOnFirstUse
                    )
                }

        val exportData =
                ExportData(exportedAt = dateFormat.format(Date()), servers = exportableConfigs)

        return json.encodeToString(exportData)
    }

    /**
     * Import server configurations from JSON. Returns a list of ServerConfig objects (without
     * passwords).
     */
    fun import(jsonString: String): List<ServerConfig> {
        val exportData = json.decodeFromString<ExportData>(jsonString)

        return exportData.servers.map { exported ->
            ServerConfig(
                    id = 0, // New ID will be assigned on save
                    serverName = exported.serverName,
                    hostname = exported.hostname,
                    port = exported.port,
                    useSsl = exported.useSsl,
                    acceptSelfSignedCerts = exported.acceptSelfSignedCerts,
                    nickname = exported.nickname,
                    altNickname = exported.altNickname,
                    username = exported.username,
                    realName = exported.realName,
                    useSasl = exported.useSasl,
                    saslUsername = exported.saslUsername,
                    autoConnect = exported.autoConnect,
                    autoJoinChannels = exported.autoJoinChannels,
                    onConnectCommands = exported.onConnectCommands,
                    nickServCommand = exported.nickServCommand,
                    trustOnFirstUse = exported.trustOnFirstUse
                    // Passwords must be re-entered by user
                    )
        }
    }

    /** Validate import data format. */
    fun validate(jsonString: String): Boolean {
        return try {
            val data = json.decodeFromString<ExportData>(jsonString)
            data.servers.isNotEmpty()
        } catch (e: Exception) {
            false
        }
    }
}
