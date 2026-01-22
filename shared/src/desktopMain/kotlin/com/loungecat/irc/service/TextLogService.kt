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
    fun readLastMessages(
            serverId: Long,
            serverName: String,
            channelName: String,
            limit: Int
    ): List<IncomingMessage> {
        val messages = mutableListOf<IncomingMessage>()
        try {
            // Sanitize names for file paths (same logic as writing)
            val safeServerName = serverName.replace(Regex("[^a-zA-Z0-9_#-]"), "_")
            val safeChannelName = channelName.replace(Regex("[^a-zA-Z0-9_#-]"), "_")

            val serverDir = File(logDir, safeServerName)
            val logFile = File(serverDir, "$safeChannelName.log")

            if (logFile.exists()) {
                // Read file line by line
                // Ideally this should look at the END of the file efficiently for large logs,
                // but for now reading all lines and taking last N is simplest.
                // Optimization: RandomAccessFile could be used for reading from end.
                // Given the user wants history, let's just use readLines() which is safer than
                // complex seeking
                // for variable length UTF-8.
                val lines = logFile.readLines()
                val linesToProcess = if (lines.size > limit) lines.takeLast(limit) else lines

                linesToProcess.forEach { line ->
                    parseLogLine(line, channelName)?.let { messages.add(it) }
                }
            }
        } catch (e: Exception) {
            Logger.e("TextLogService", "Failed to read logs for $channelName", e)
        }
        return messages
    }

    private fun parseLogLine(line: String, channelName: String): IncomingMessage? {
        try {
            // Format: [yyyy-MM-dd HH:mm:ss] <sender> content
            // Regex to parse
            val regex = Regex("^\\[(.*?)\\] <(.*?)> (.*)$")
            val matchResult = regex.find(line)

            if (matchResult != null) {
                val (dateStr, sender, content) = matchResult.destructured
                val timestamp = dateFormat.parse(dateStr)?.time ?: System.currentTimeMillis()

                return IncomingMessage(
                        timestamp = timestamp,
                        sender = sender,
                        target = channelName,
                        content = content,
                        type = com.loungecat.irc.data.model.MessageType.NORMAL,
                        isSelf = false // We can't easily know if it was self, assuming false/other
                )
            }
        } catch (e: Exception) {
            // Ignore parse errors
        }
        return null
    }
}
