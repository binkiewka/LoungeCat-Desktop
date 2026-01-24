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

        // Fix legacy directory names (merge split folders)
        fixLegacyDirectoryNames()
    }

    private fun fixLegacyDirectoryNames() {
        try {
            val root = logDir ?: return
            val dirs = root.listFiles { file -> file.isDirectory } ?: return

            // Group by "base" name (without trailing underscore)
            // We are looking for pairs: "Name" and "Name_"
            // "Name_" is likely the OLD folder (creation date older?) or the one we want to
            // keep/merge into "Name"
            // Wait, looking at the user image:
            // "Libera_Chat" (21.01 11:40) - New
            // "Libera_Chat_" (20.01 23:20) - Old
            // "PT" (21.01)

            // So "Name_" is the OLD one. "Name" is the NEW one (created after regression).
            // We want to merge "Name" (new partial logs) INTO "Name_" (full history),
            // and then rename "Name_" -> "Name" (standard).

            val validDirs = dirs.map { it.name }.toSet()

            dirs.forEach { dir ->
                val name = dir.name
                if (name.endsWith("_")) {
                    val baseName = name.removeSuffix("_")
                    val newDir = File(root, baseName)

                    if (newDir.exists() && newDir.isDirectory) {
                        Logger.d(
                                "TextLogService",
                                "Found split log directories: $name (Old) and $baseName (New). Merging..."
                        )
                        mergeDirectories(source = newDir, destination = dir)

                        // After merge, rename "New" to backup/delete and "Old" to "New"
                        val backupDir = File(root, "${baseName}_bak")
                        if (newDir.renameTo(backupDir)) {
                            if (dir.renameTo(newDir)) {
                                Logger.d(
                                        "TextLogService",
                                        "Successfully restored log history for $baseName"
                                )
                                // Optional: Delete backup if empty or safe? specific request was
                                // just to fix.
                                // limit risk by keeping _bak for now or user can delete.
                            } else {
                                Logger.e("TextLogService", "Failed to rename $name to $baseName")
                                // rollback?
                                backupDir.renameTo(newDir)
                            }
                        } else {
                            Logger.e("TextLogService", "Failed to move new logs to backup")
                        }
                    } else {
                        // Case: Only "Name_" exists, and we want it to be "Name"
                        // This restores the standard naming if the new folder hasn't been created
                        // yet
                        val targetDir = File(root, baseName)
                        if (!targetDir.exists()) {
                            if (dir.renameTo(targetDir)) {
                                Logger.d(
                                        "TextLogService",
                                        "Renamed legacy folder $name to $baseName"
                                )
                            }
                        }
                    }
                }
            }
        } catch (e: Exception) {
            Logger.e("TextLogService", "Error fixing legacy directories", e)
        }
    }

    private fun mergeDirectories(source: File, destination: File) {
        val srcFiles = source.listFiles() ?: return

        srcFiles.forEach { srcFile ->
            val destFile = File(destination, srcFile.name)
            if (destFile.exists()) {
                // Append content
                try {
                    destFile.appendBytes(srcFile.readBytes())
                    Logger.d("TextLogService", "Merged content for ${srcFile.name}")
                    srcFile.delete()
                } catch (e: Exception) {
                    Logger.e("TextLogService", "Failed to merge ${srcFile.name}", e)
                }
            } else {
                // Move file
                if (srcFile.renameTo(destFile)) {
                    Logger.d("TextLogService", "Moved ${srcFile.name}")
                } else {
                    Logger.e("TextLogService", "Failed to move ${srcFile.name}")
                }
            }
        }
        // Try to delete empty source dir
        source.delete()
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
                readLogFile(logFile, limit, channelName, messages)
            } else {
                // FALLBACK: Check for legacy filename where '#' might have been replaced by '_'
                // or just standard legacy sanitization.
                val legacySafeName = safeChannelName.replace("#", "_")
                val legacyFile = File(serverDir, "$legacySafeName.log")
                if (legacyFile.exists()) {
                    Logger.d("TextLogService", "Found legacy log file: ${legacyFile.name}")
                    readLogFile(legacyFile, limit, channelName, messages)
                } else {
                    // FALLBACK 2: Case-insensitive search
                    // This is expensive so only do it if direct lookups fail
                    val foundFile =
                            serverDir.listFiles()?.find {
                                it.name.equals("$safeChannelName.log", ignoreCase = true) ||
                                        it.name.equals("$legacySafeName.log", ignoreCase = true)
                            }
                    if (foundFile != null) {
                        Logger.d(
                                "TextLogService",
                                "Found log file via fuzzy search: ${foundFile.name}"
                        )
                        readLogFile(foundFile, limit, channelName, messages)
                    }
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
    private fun readLogFile(
            file: File,
            limit: Int,
            channelName: String,
            messages: MutableList<IncomingMessage>
    ) {
        try {
            val lines = file.readLines()
            val linesToProcess = if (lines.size > limit) lines.takeLast(limit) else lines

            linesToProcess.forEach { line ->
                parseLogLine(line, channelName)?.let { messages.add(it) }
            }
        } catch (e: Exception) {
            Logger.e("TextLogService", "Failed to read log file: ${file.name}", e)
        }
    }
}
