package com.loungecat.irc.util

/**
 * Manages input history for each channel, allowing users to navigate through previously sent
 * messages using arrow keys.
 */
class InputHistoryHelper(private val maxSize: Int = 50) {
    // Map<channelKey, List<messages>> - most recent at end
    private val histories = mutableMapOf<String, MutableList<String>>()
    // Current navigation index per channel (-1 means at current input, not in history)
    private val currentIndexes = mutableMapOf<String, Int>()
    // Saves the current input when user starts navigating
    private val tempInputs = mutableMapOf<String, String>()

    /** Add a sent message to the history for a channel. Also resets navigation state. */
    fun addMessage(channel: String, message: String) {
        if (message.isBlank()) return

        val history = histories.getOrPut(channel) { mutableListOf() }

        // Don't add duplicate of the last message
        if (history.lastOrNull() == message) {
            resetNavigation(channel)
            return
        }

        history.add(message)

        // Trim to max size
        while (history.size > maxSize) {
            history.removeAt(0)
        }

        resetNavigation(channel)
    }

    /**
     * Navigate up (back in history). Returns the message to display, or null if at the start of
     * history.
     */
    fun navigateUp(channel: String, currentInput: String): String? {
        val history = histories[channel] ?: return null
        if (history.isEmpty()) return null

        val currentIndex = currentIndexes[channel] ?: -1

        // If starting navigation, save current input
        if (currentIndex == -1) {
            tempInputs[channel] = currentInput
        }

        // Calculate new index
        val newIndex =
                if (currentIndex == -1) {
                    history.size - 1 // Start at most recent
                } else {
                    (currentIndex - 1).coerceAtLeast(0)
                }

        currentIndexes[channel] = newIndex
        return history[newIndex]
    }

    /**
     * Navigate down (forward in history, toward current input). Returns the message to display, or
     * null if past end of history.
     */
    fun navigateDown(channel: String): String? {
        val history = histories[channel] ?: return null
        val currentIndex = currentIndexes[channel] ?: return null

        if (currentIndex == -1) return null // Already at current input

        val newIndex = currentIndex + 1

        return if (newIndex >= history.size) {
            // Return to current input
            currentIndexes[channel] = -1
            tempInputs[channel] ?: ""
        } else {
            currentIndexes[channel] = newIndex
            history[newIndex]
        }
    }

    /** Check if currently navigating history for a channel. */
    fun isNavigating(channel: String): Boolean {
        return (currentIndexes[channel] ?: -1) != -1
    }

    /** Reset navigation state for a channel (used when input changes or message sent). */
    fun resetNavigation(channel: String) {
        currentIndexes[channel] = -1
        tempInputs.remove(channel)
    }

    /** Clear all history for a channel. */
    fun clearHistory(channel: String) {
        histories.remove(channel)
        resetNavigation(channel)
    }

    /** Get current history size for a channel. */
    fun getHistorySize(channel: String): Int = histories[channel]?.size ?: 0
}
