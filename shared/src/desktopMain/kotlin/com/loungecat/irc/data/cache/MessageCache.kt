package com.loungecat.irc.data.cache

import com.loungecat.irc.data.model.CachedChannel
import com.loungecat.irc.data.model.CachedMessage
import com.loungecat.irc.data.model.IncomingMessage
import com.loungecat.irc.util.Logger
import java.io.File
import kotlinx.coroutines.*
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

object MessageCache {
    private val json = Json {
        prettyPrint = false
        ignoreUnknownKeys = true
    }

    private var cacheDir: File? = null
    private val cacheScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private var saveJob: Job? = null
    private const val MAX_MESSAGES_PER_CHANNEL = 2000
    private const val SAVE_DEBOUNCE_MS = 2000L
    private const val DEFAULT_PAGE_SIZE = 100

    fun initialize(appDataDir: File) {
        cacheDir = File(appDataDir, "cache/messages")
        cacheDir?.mkdirs()
        Logger.d("MessageCache", "Initialized at ${cacheDir?.absolutePath}")
    }

    private fun getCacheFile(serverId: Long, channelName: String): File? {
        val safeName = channelName.replace(Regex("[^a-zA-Z0-9_#-]"), "_")
        return cacheDir?.let { File(it, "${serverId}_${safeName}.json") }
    }

    fun loadMessages(serverId: Long, channelName: String): List<IncomingMessage> {
        return loadMessages(serverId, channelName, 0, MAX_MESSAGES_PER_CHANNEL)
    }

    /**
     * Load messages with pagination support.
     * @param serverId Server ID
     * @param channelName Channel name
     * @param offset Number of messages to skip from the end (most recent)
     * @param limit Maximum number of messages to return
     * @return List of messages, newest last
     */
    fun loadMessages(
            serverId: Long,
            channelName: String,
            offset: Int,
            limit: Int
    ): List<IncomingMessage> {
        return try {
            val file = getCacheFile(serverId, channelName)
            if (file?.exists() == true) {
                val content = file.readText()
                val cached = json.decodeFromString<CachedChannel>(content)
                val allMessages = cached.messages.map { it.toIncomingMessage() }

                // Calculate pagination - offset from end, return in chronological order
                val endIndex = (allMessages.size - offset).coerceAtLeast(0)
                val startIndex = (endIndex - limit).coerceAtLeast(0)

                Logger.d(
                        "MessageCache",
                        "Loaded messages [$startIndex..$endIndex] of ${allMessages.size} for $channelName"
                )
                allMessages.subList(startIndex, endIndex)
            } else {
                emptyList()
            }
        } catch (e: Exception) {
            Logger.e("MessageCache", "Failed to load messages for $channelName", e)
            emptyList()
        }
    }

    /** Get total message count for a channel. */
    fun getMessageCount(serverId: Long, channelName: String): Int {
        return try {
            val file = getCacheFile(serverId, channelName)
            if (file?.exists() == true) {
                val content = file.readText()
                val cached = json.decodeFromString<CachedChannel>(content)
                cached.messages.size
            } else {
                0
            }
        } catch (e: Exception) {
            Logger.e("MessageCache", "Failed to get message count for $channelName", e)
            0
        }
    }

    fun saveMessages(serverId: Long, channelName: String, messages: List<IncomingMessage>) {
        // Cancel previous save job to debounce
        saveJob?.cancel()
        saveJob =
                cacheScope.launch {
                    delay(SAVE_DEBOUNCE_MS)
                    saveMessagesSync(serverId, channelName, messages)
                }
    }

    private fun saveMessagesSync(
            serverId: Long,
            channelName: String,
            messages: List<IncomingMessage>
    ) {
        try {
            val file = getCacheFile(serverId, channelName)
            file?.let {
                it.parentFile?.mkdirs()

                // Limit to last N messages
                val limitedMessages =
                        if (messages.size > MAX_MESSAGES_PER_CHANNEL) {
                            messages.takeLast(MAX_MESSAGES_PER_CHANNEL)
                        } else {
                            messages
                        }

                val cached =
                        CachedChannel(
                                serverId = serverId,
                                channelName = channelName,
                                messages =
                                        limitedMessages.map { msg ->
                                            CachedMessage.fromIncomingMessage(msg)
                                        }
                        )

                val content = json.encodeToString(cached)
                it.writeText(content)
                Logger.d("MessageCache", "Saved ${limitedMessages.size} messages for $channelName")
            }
        } catch (e: Exception) {
            Logger.e("MessageCache", "Failed to save messages for $channelName", e)
        }
    }

    fun clearCache(serverId: Long, channelName: String) {
        try {
            getCacheFile(serverId, channelName)?.delete()
            Logger.d("MessageCache", "Cleared cache for $channelName")
        } catch (e: Exception) {
            Logger.e("MessageCache", "Failed to clear cache for $channelName", e)
        }
    }

    fun clearAllCache() {
        try {
            cacheDir?.listFiles()?.forEach { it.delete() }
            Logger.d("MessageCache", "Cleared all message cache")
        } catch (e: Exception) {
            Logger.e("MessageCache", "Failed to clear all cache", e)
        }
    }

    fun shutdown() {
        cacheScope.cancel()
    }
}
