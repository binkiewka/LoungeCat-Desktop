package com.loungecat.irc.service

import com.loungecat.irc.data.model.IncomingMessage
import com.loungecat.irc.util.Logger
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

object TextLogService {
    private var logDir: File? = null
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault())

    fun initialize(appDataDir: File) {
        logDir = File(appDataDir, "logs")
        logDir?.mkdirs()
        Logger.d("TextLogService", "Initialized at ${logDir?.absolutePath}")
    }

    fun logFromMessage(
            serverId: Long,
            serverName: String,
            channelName: String,
            message: IncomingMessage
    ) {
        scope.launch {
            try {
                // Sanitize names for file paths
                val safeServerName = serverName.replace(Regex("[^a-zA-Z0-9_#-]"), "_")
                val safeChannelName = channelName.replace(Regex("[^a-zA-Z0-9_#-]"), "_")

                // Create server directory
                val serverDir = File(logDir, safeServerName)
                if (!serverDir.exists()) {
                    serverDir.mkdirs()
                }

                // Log file per channel
                val logFile = File(serverDir, "$safeChannelName.log")

                val timestamp = dateFormat.format(Date(message.timestamp))
                val logLine = "[$timestamp] <${message.sender}> ${message.content}\n"

                logFile.appendText(logLine)
            } catch (e: Exception) {
                Logger.e("TextLogService", "Failed to log message for $channelName", e)
            }
        }
    }
}
