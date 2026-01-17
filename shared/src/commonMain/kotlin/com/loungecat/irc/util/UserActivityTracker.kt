package com.loungecat.irc.util

/**
 * Tracks when users last spoke in each channel. Used for "smart hide" feature to filter
 * join/part/quit messages for users who haven't been active recently.
 */
class UserActivityTracker {
    // Map<channelName, Map<nickname (lowercase), lastActivityTimestamp>>
    private val channelActivity = mutableMapOf<String, MutableMap<String, Long>>()

    /**
     * Record that a user spoke in a channel. Should be called when receiving NORMAL or ACTION
     * messages.
     */
    fun recordActivity(channel: String, nickname: String) {
        val channelMap = channelActivity.getOrPut(channel) { mutableMapOf() }
        channelMap[nickname.lowercase()] = System.currentTimeMillis()
    }

    /**
     * Check if a user was recently active in a channel.
     * @param channel The channel name
     * @param nickname The user's nickname
     * @param thresholdMinutes How many minutes to consider as "recent"
     * @return true if the user spoke within the threshold, false otherwise
     */
    fun wasRecentlyActive(channel: String, nickname: String, thresholdMinutes: Int): Boolean {
        val channelMap = channelActivity[channel] ?: return false
        val lastActivity = channelMap[nickname.lowercase()] ?: return false

        val thresholdMs = thresholdMinutes * 60 * 1000L
        val elapsed = System.currentTimeMillis() - lastActivity

        return elapsed <= thresholdMs
    }

    /** Clear activity data for a channel (e.g., when leaving). */
    fun clearChannel(channel: String) {
        channelActivity.remove(channel)
    }

    /** Clear activity for a specific user across all channels. */
    fun clearUser(nickname: String) {
        val lowerNick = nickname.lowercase()
        channelActivity.values.forEach { it.remove(lowerNick) }
    }

    /** Get the number of tracked users in a channel. */
    fun getTrackedUserCount(channel: String): Int {
        return channelActivity[channel]?.size ?: 0
    }
}
