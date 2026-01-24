package com.loungecat.irc.data.database

import com.loungecat.irc.data.model.ServerConfig
import com.loungecat.irc.service.DatabaseServiceInterface
import com.loungecat.irc.util.Logger
import com.loungecat.irc.util.PlatformUtils
import java.io.File
import java.sql.Connection
import java.sql.DriverManager
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

object DatabaseService : DatabaseServiceInterface {

    private var connection: Connection? = null

    fun initialize() {
        try {
            val dbDir = File(PlatformUtils.getAppDataDirectory())
            if (!dbDir.exists()) {
                dbDir.mkdirs()
            }

            val dbPath = PlatformUtils.getDatabasePath()
            connection = DriverManager.getConnection("jdbc:sqlite:$dbPath")

            createTables()
            performMigrations()
            Logger.d("DatabaseService", "Database initialized at $dbPath")
        } catch (e: Exception) {
            Logger.e("DatabaseService", "CRITICAL: Failed to initialize database", e)
        }
    }

    private fun createTables() {
        connection
                ?.createStatement()
                ?.execute(
                        """
            CREATE TABLE IF NOT EXISTS server_configs (
                id INTEGER PRIMARY KEY,
                server_name TEXT NOT NULL,
                hostname TEXT NOT NULL,
                port INTEGER NOT NULL DEFAULT 6697,
                use_ssl INTEGER NOT NULL DEFAULT 1,
                nickname TEXT NOT NULL,
                alt_nickname TEXT DEFAULT '',
                username TEXT,
                real_name TEXT,
                use_sasl INTEGER NOT NULL DEFAULT 0,
                sasl_username TEXT,
                sasl_password TEXT,
                auto_join_channels TEXT,
                on_connect_commands TEXT,
                nick_serv_password TEXT,
                nick_serv_command TEXT,
                pinned_cert_fingerprint TEXT,
                trust_on_first_use INTEGER DEFAULT 0,
                auto_connect INTEGER DEFAULT 1,
                proxy_type TEXT,
                proxy_host TEXT,
                proxy_port INTEGER,
                proxy_username TEXT,
                proxy_password TEXT
            )
        """.trimIndent()
                )
    }

    private fun performMigrations() {
        try {
            val stmt = connection?.createStatement() ?: return

            // Check existing columns to avoid errors
            val columns = mutableSetOf<String>()
            val rs = stmt.executeQuery("PRAGMA table_info(server_configs)")
            while (rs.next()) {
                columns.add(rs.getString("name"))
            }
            rs.close()

            // Add missing columns if they don't exist
            val migrations =
                    mapOf(
                            "alt_nickname" to "TEXT DEFAULT ''",
                            "on_connect_commands" to "TEXT",
                            "nick_serv_password" to "TEXT",
                            "nick_serv_command" to "TEXT",
                            "pinned_cert_fingerprint" to "TEXT",
                            "trust_on_first_use" to "INTEGER DEFAULT 0",
                            "auto_connect" to "INTEGER DEFAULT 1",
                            "proxy_type" to "TEXT",
                            "proxy_host" to "TEXT",
                            "proxy_port" to "INTEGER",
                            "proxy_username" to "TEXT",
                            "proxy_password" to "TEXT"
                    )

            for ((column, type) in migrations) {
                if (!columns.contains(column)) {
                    Logger.d("DatabaseService", "Migrating: Adding column $column")
                    try {
                        stmt.execute("ALTER TABLE server_configs ADD COLUMN $column $type")
                    } catch (e: Exception) {
                        Logger.e("DatabaseService", "Failed to add column $column", e)
                    }
                }
            }
        } catch (e: Exception) {
            Logger.e("DatabaseService", "Failed to perform migrations", e)
        }
    }

    override suspend fun getAllServerConfigs(): List<ServerConfig> =
            withContext(Dispatchers.IO) {
                val configs = mutableListOf<ServerConfig>()
                try {
                    connection
                            ?.createStatement()
                            ?.executeQuery("SELECT * FROM server_configs")
                            ?.use { rs ->
                                while (rs.next()) {
                                    configs.add(
                                            ServerConfig(
                                                    id = rs.getLong("id"),
                                                    serverName = rs.getString("server_name"),
                                                    hostname = rs.getString("hostname"),
                                                    port = rs.getInt("port"),
                                                    useSsl = rs.getInt("use_ssl") == 1,
                                                    nickname = rs.getString("nickname"),
                                                    altNickname = rs.getString("alt_nickname")
                                                                    ?: "",
                                                    username = rs.getString("username"),
                                                    realName = rs.getString("real_name"),
                                                    useSasl = rs.getInt("use_sasl") == 1,
                                                    saslUsername = rs.getString("sasl_username"),
                                                    saslPassword = rs.getString("sasl_password"),
                                                    autoJoinChannels =
                                                            rs.getString("auto_join_channels")
                                                                    ?: "",
                                                    onConnectCommands =
                                                            rs.getString("on_connect_commands")
                                                                    ?: "",
                                                    nickServPassword =
                                                            rs.getString("nick_serv_password"),
                                                    nickServCommand =
                                                            rs.getString("nick_serv_command")
                                                                    ?: "IDENTIFY",
                                                    pinnedCertFingerprint =
                                                            rs.getString("pinned_cert_fingerprint"),
                                                    trustOnFirstUse =
                                                            rs.getInt("trust_on_first_use") == 1,
                                                    autoConnect = rs.getInt("auto_connect") == 1,
                                                    proxyType =
                                                            try {
                                                                val type =
                                                                        rs.getString("proxy_type")
                                                                if (type != null)
                                                                        com.loungecat.irc.data.model
                                                                                .ProxyType.valueOf(
                                                                                type
                                                                        )
                                                                else
                                                                        com.loungecat.irc.data.model
                                                                                .ProxyType.NONE
                                                            } catch (e: Exception) {
                                                                com.loungecat.irc.data.model
                                                                        .ProxyType.NONE
                                                            },
                                                    proxyHost = rs.getString("proxy_host") ?: "",
                                                    proxyPort =
                                                            rs.getInt("proxy_port").let {
                                                                if (rs.wasNull()) 1080 else it
                                                            },
                                                    proxyUsername = rs.getString("proxy_username"),
                                                    proxyPassword = rs.getString("proxy_password")
                                            )
                                    )
                                }
                            }
                } catch (e: Exception) {
                    Logger.e("DatabaseService", "Failed to load server configs", e)
                }
                configs
            }

    override suspend fun insertServerConfig(config: ServerConfig): Long =
            withContext(Dispatchers.IO) {
                try {
                    val sql =
                            """
                INSERT INTO server_configs 
                (id, server_name, hostname, port, use_ssl, nickname, alt_nickname, username, real_name, 
                 use_sasl, sasl_username, sasl_password, auto_join_channels,
                 on_connect_commands, nick_serv_password, nick_serv_command,
                 pinned_cert_fingerprint, trust_on_first_use, auto_connect,
                 proxy_type, proxy_host, proxy_port, proxy_username, proxy_password)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
            """.trimIndent()

                    connection?.prepareStatement(sql)?.apply {
                        var idx = 1
                        setLong(idx++, config.id)
                        setString(idx++, config.serverName)
                        setString(idx++, config.hostname)
                        setInt(idx++, config.port)
                        setInt(idx++, if (config.useSsl) 1 else 0)
                        setString(idx++, config.nickname)
                        setString(idx++, config.altNickname)
                        setString(idx++, config.username)
                        setString(idx++, config.realName)
                        setInt(idx++, if (config.useSasl) 1 else 0)
                        setString(idx++, config.saslUsername)
                        setString(idx++, config.saslPassword)
                        setString(idx++, config.autoJoinChannels)
                        setString(idx++, config.onConnectCommands)
                        setString(idx++, config.nickServPassword)
                        setString(idx++, config.nickServCommand)
                        setString(idx++, config.pinnedCertFingerprint)
                        setInt(idx++, if (config.trustOnFirstUse) 1 else 0)
                        setInt(idx++, if (config.autoConnect) 1 else 0)
                        setString(idx++, config.proxyType.name)
                        setString(idx++, config.proxyHost)
                        setInt(idx++, config.proxyPort)
                        setString(idx++, config.proxyUsername)
                        setString(idx++, config.proxyPassword)
                        executeUpdate()
                    }
                    config.id
                } catch (e: Exception) {
                    Logger.e("DatabaseService", "Failed to insert server config", e)
                    -1
                }
            }

    override suspend fun updateServerConfig(config: ServerConfig): Unit =
            withContext(Dispatchers.IO) {
                try {
                    val sql =
                            """
                UPDATE server_configs SET
                server_name = ?, hostname = ?, port = ?, use_ssl = ?, nickname = ?, alt_nickname = ?,
                username = ?, real_name = ?, use_sasl = ?, sasl_username = ?, sasl_password = ?, auto_join_channels = ?,
                on_connect_commands = ?, nick_serv_password = ?, nick_serv_command = ?,
                pinned_cert_fingerprint = ?, trust_on_first_use = ?, auto_connect = ?,
                proxy_type = ?, proxy_host = ?, proxy_port = ?, proxy_username = ?, proxy_password = ?
                WHERE id = ?
            """.trimIndent()

                    connection?.prepareStatement(sql)?.apply {
                        var idx = 1
                        setString(idx++, config.serverName)
                        setString(idx++, config.hostname)
                        setInt(idx++, config.port)
                        setInt(idx++, if (config.useSsl) 1 else 0)
                        setString(idx++, config.nickname)
                        setString(idx++, config.altNickname)
                        setString(idx++, config.username)
                        setString(idx++, config.realName)
                        setInt(idx++, if (config.useSasl) 1 else 0)
                        setString(idx++, config.saslUsername)
                        setString(idx++, config.saslPassword)
                        setString(idx++, config.autoJoinChannels)
                        setString(idx++, config.onConnectCommands)
                        setString(idx++, config.nickServPassword)
                        setString(idx++, config.nickServCommand)
                        setString(idx++, config.pinnedCertFingerprint)
                        setInt(idx++, if (config.trustOnFirstUse) 1 else 0)
                        setInt(idx++, if (config.autoConnect) 1 else 0)
                        setString(idx++, config.proxyType.name)
                        setString(idx++, config.proxyHost)
                        setInt(idx++, config.proxyPort)
                        setString(idx++, config.proxyUsername)
                        setString(idx++, config.proxyPassword)

                        setLong(idx++, config.id)
                        executeUpdate()
                    }
                } catch (e: Exception) {
                    Logger.e("DatabaseService", "Failed to update server config", e)
                }
            }

    override suspend fun deleteServerConfig(id: Long): Unit =
            withContext(Dispatchers.IO) {
                try {
                    connection?.prepareStatement("DELETE FROM server_configs WHERE id = ?")?.apply {
                        setLong(1, id)
                        executeUpdate()
                    }
                } catch (e: Exception) {
                    Logger.e("DatabaseService", "Failed to delete server config", e)
                }
            }

    fun close() {
        connection?.close()
        connection = null
    }
}
