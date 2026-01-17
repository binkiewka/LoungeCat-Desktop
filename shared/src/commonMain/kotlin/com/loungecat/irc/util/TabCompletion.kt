package com.loungecat.irc.util

import com.loungecat.irc.data.model.ChannelUser

/**
 * Tab completion helper for IRC chat input. Supports completing nicknames, commands, and channel
 * names.
 */
class TabCompletionHelper {
    private var currentIndex = -1
    private var currentPrefix = ""
    private var currentCursorPosition = 0
    private var candidates: List<String> = emptyList()
    private var wordStart = 0
    private var wordEnd = 0

    // State for proper cycling
    private var baseInput = ""
    private var isAtMessageStart = false

    companion object {
        // All available IRC commands for completion
        val COMMANDS =
                listOf(
                        "/join",
                        "/j",
                        "/part",
                        "/leave",
                        "/cycle",
                        "/rejoin",
                        "/invite",
                        "/topic",
                        "/t",
                        "/list",
                        "/names",
                        "/msg",
                        "/privmsg",
                        "/query",
                        "/q",
                        "/me",
                        "/notice",
                        "/whois",
                        "/wii",
                        "/who",
                        "/nick",
                        "/away",
                        "/back",
                        "/ignore",
                        "/unignore",
                        "/kick",
                        "/k",
                        "/ban",
                        "/b",
                        "/unban",
                        "/voice",
                        "/v",
                        "/devoice",
                        "/op",
                        "/deop",
                        "/quiet",
                        "/mute",
                        "/unquiet",
                        "/unmute",
                        "/mode",
                        "/quit",
                        "/disconnect",
                        "/exit",
                        "/raw",
                        "/quote",
                        "/ping",
                        "/time",
                        "/version",
                        "/motd",
                        "/info",
                        "/connect",
                        "/server",
                        "/clear",
                        "/cls",
                        "/help",
                        "/commands"
                )
    }

    /**
     * Get completion candidates for the current input at cursor position.
     * @param input Current input text
     * @param cursorPosition Cursor position in input
     * @param channelUsers Users in current channel
     * @param channelNames Available channel names
     * @param isAtStart Whether the cursor is at the start of the message (affects formatting)
     * @return true if candidates were found
     */
    fun initCompletion(
            input: String,
            cursorPosition: Int,
            channelUsers: List<ChannelUser>,
            channelNames: List<String>,
            isAtStart: Boolean = false
    ): Boolean {
        // Find word boundaries at cursor
        wordStart = findWordStart(input, cursorPosition)
        wordEnd = findWordEnd(input, cursorPosition)
        currentPrefix = input.substring(wordStart, cursorPosition).lowercase()
        currentCursorPosition = cursorPosition

        // Store state for cycling
        baseInput = input
        isAtMessageStart = isAtStart

        if (currentPrefix.isEmpty()) {
            reset()
            return false
        }

        candidates =
                when {
                    // Command completion (starts with /)
                    currentPrefix.startsWith("/") -> {
                        COMMANDS.filter { it.startsWith(currentPrefix, ignoreCase = true) }
                    }
                    // Channel completion (starts with #)
                    currentPrefix.startsWith("#") -> {
                        channelNames.filter { it.startsWith(currentPrefix, ignoreCase = true) }
                    }
                    // Nickname completion
                    else -> {
                        channelUsers
                                .map { it.nickname }
                                .filter { it.startsWith(currentPrefix, ignoreCase = true) }
                                .sortedBy { it.lowercase() }
                    }
                }

        currentIndex = -1
        return candidates.isNotEmpty()
    }

    /**
     * Get the next completion candidate.
     * @return The new input text with completion applied, or null if no more candidates
     */
    fun cycleNext(): String? {
        if (candidates.isEmpty()) return null

        currentIndex = (currentIndex + 1) % candidates.size
        return applyCompletion()
    }

    /** Get the previous completion candidate (Shift+Tab). */
    fun cyclePrevious(): String? {
        if (candidates.isEmpty()) return null

        currentIndex = if (currentIndex <= 0) candidates.size - 1 else currentIndex - 1
        return applyCompletion()
    }

    private fun applyCompletion(): String {
        val completion = candidates[currentIndex]
        val prefix = baseInput.substring(0, wordStart)
        val suffix = baseInput.substring(wordEnd)

        // Add ": " suffix for nicknames at start of message
        val completionSuffix =
                if (isAtMessageStart &&
                                !completion.startsWith("/") &&
                                !completion.startsWith("#") &&
                                wordStart == 0
                ) {
                    ": "
                } else if (suffix.isEmpty() || suffix[0] != ' ') {
                    " "
                } else {
                    ""
                }

        return prefix + completion + completionSuffix + suffix
    }

    /** Get cursor position after applying completion. */
    fun getNewCursorPosition(newInput: String): Int {
        if (candidates.isEmpty() || currentIndex < 0) return currentCursorPosition

        val completion = candidates[currentIndex]
        // Position cursor after the completion + any suffix
        val basePos = wordStart + completion.length

        // Check if we added ": " or " "
        val afterCompletion = newInput.substring(basePos)
        return when {
            afterCompletion.startsWith(": ") -> basePos + 2
            afterCompletion.startsWith(" ") -> basePos + 1
            else -> basePos
        }
    }

    /** Reset completion state. Call when input changes by typing. */
    fun reset() {
        currentIndex = -1
        currentPrefix = ""
        candidates = emptyList()
        wordStart = 0
        wordEnd = 0
        baseInput = ""
        isAtMessageStart = false
    }

    /** Check if we're in an active completion cycle. */
    fun isActive(): Boolean = candidates.isNotEmpty() && currentIndex >= 0

    /** Get current candidate count. */
    fun getCandidateCount(): Int = candidates.size

    private fun findWordStart(input: String, cursorPos: Int): Int {
        var pos = cursorPos - 1
        while (pos >= 0 && !input[pos].isWhitespace()) {
            pos--
        }
        return pos + 1
    }

    private fun findWordEnd(input: String, cursorPos: Int): Int {
        var pos = cursorPos
        while (pos < input.length && !input[pos].isWhitespace()) {
            pos++
        }
        return pos
    }
}
