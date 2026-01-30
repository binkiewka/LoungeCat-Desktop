package com.loungecat.irc.service

import com.loungecat.irc.data.model.IncomingMessage
import com.loungecat.irc.util.Logger
import java.io.File
import java.text.SimpleDateFormat
import java.time.LocalDateTime
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

    private fun debugLog(message: String) {
        try {
            val file = File(System.getProperty("user.home"), ".loungecat/debug_history.txt")
            val timestamp = LocalDateTime.now().toString()
            file.appendText("[$timestamp] $message\n")
        } catch (e: Exception) {
            // Ignore
        }
    }

    private fun getBestServerDirectory(safeServerName: String): File {
        val root = logDir ?: return File(safeServerName) // Should not happen if init called

        // 1. Try exact match
        val exactMatch = File(root, safeServerName)
        if (exactMatch.exists() && exactMatch.isDirectory) {
            return exactMatch
        }

        // 2. Try case-insensitive match to reuse existing legacy folders
        val caseInsensitiveMatch =
                root.listFiles()?.find {
                    it.isDirectory && it.name.equals(safeServerName, ignoreCase = true)
                }

        return caseInsensitiveMatch ?: exactMatch
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

                // Resolve directory: Use existing if available (ignoring case) to prevent splits
                val serverDir = getBestServerDirectory(safeServerName)
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
        debugLog("readLastMessages called for Server: '$serverName', Channel: '$channelName'")
        val messages = mutableListOf<IncomingMessage>()
        try {
            // Sanitize names
            val safeServerName = serverName.replace(Regex("[^a-zA-Z0-9_#-]"), "_")
            val safeChannelName = channelName.replace(Regex("[^a-zA-Z0-9_#-]"), "_")

            debugLog("Sanitized: Server='$safeServerName', Channel='$safeChannelName'")

            // Identify ALL candidate directories (exact + case variants)
            val root = logDir
            val candidateDirs = mutableSetOf<File>()

            if (root != null && root.exists()) {
                // 1. Possible exact match
                candidateDirs.add(File(root, safeServerName))

                // 2. Search for any other case variants
                root.listFiles()?.forEach { file ->
                    if (file.isDirectory && file.name.equals(safeServerName, ignoreCase = true)) {
                        candidateDirs.add(file)
                    }
                }
            }

            val validDirs = candidateDirs.filter { it.exists() && it.isDirectory }
            debugLog("Found candidates: ${validDirs.map { it.name }}")

            // Read from ALL valid candidates
            validDirs.forEach { dir ->
                val logFile = File(dir, "$safeChannelName.log")
                debugLog("Checking file: ${logFile.absolutePath}, exists=${logFile.exists()}")

                if (logFile.exists()) {
                    readLogFile(logFile, limit, channelName, messages)
                }

                // Also check for legacy '#' -> '_' replacement in filenames
                val legacySafeName = safeChannelName.replace("#", "_")
                val legacyFile = File(dir, "$legacySafeName.log")
                if (legacyFile.exists() && legacyFile.absolutePath != logFile.absolutePath) {
                    debugLog(
                            "Checking legacy file: ${legacyFile.absolutePath}, exists=${legacyFile.exists()}"
                    )
                    readLogFile(legacyFile, limit, channelName, messages)
                }
            }

            debugLog("Messages read count: ${messages.size}")

            // Deduplicate (simple strategy based on timestamp + sender + content)
            // We sort by timestamp first
            if (messages.isNotEmpty()) {
                return messages
                        .distinctBy { "${it.timestamp}-${it.sender}-${it.content}" }
                        .sortedBy { it.timestamp }
                        .takeLast(limit)
            }
        } catch (e: Exception) {
            debugLog("Exception: ${e.message}")
            Logger.e("TextLogService", "Failed to read logs for $channelName", e)
        }
        return messages
    }

    private fun parseLogLine(line: String, channelName: String): IncomingMessage? {
        try {
            // Format: [yyyy-MM-dd HH:mm:ss] <sender> content
            // Regex to parse. Updated to be more flexible with spacing after sender.
            val regex = Regex("^\\[(.*?)\\] <(.*?)>\\s?(.*)$")
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
                        isSelf = false
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

            var failureCount = 0
            linesToProcess.forEach { line ->
                val parsed = parseLogLine(line, channelName)
                if (parsed != null) {
                    messages.add(parsed)
                } else {
                    failureCount++
                    if (failureCount <= 10) { // Log first 10 failures max
                        debugLog("Parse Failed: '$line'")
                    }
                }
            }
            if (failureCount > 0) {
                debugLog("Total lines failed to parse in ${file.name}: $failureCount")
            }
        } catch (e: Exception) {
            Logger.e("TextLogService", "Failed to read log file: ${file.name}", e)
        }
    }
}
