package com.loungecat.irc.util

/** Utility for matching custom highlight words in messages. */
object HighlightMatcher {

    /**
     * Check if a message contains any of the highlight words.
     * @param message The message to check
     * @param highlightWords Set of words/phrases to highlight
     * @param ignoreCase Whether to match case-insensitively (default true)
     * @return true if any highlight word is found
     */
    fun containsHighlight(
            message: String,
            highlightWords: Set<String>,
            ignoreCase: Boolean = true
    ): Boolean {
        if (highlightWords.isEmpty()) return false

        return highlightWords.any { word ->
            if (word.isEmpty()) return@any false

            // Check for word boundary matches
            val regex =
                    if (ignoreCase) {
                        "\\b${Regex.escape(word)}\\b".toRegex(RegexOption.IGNORE_CASE)
                    } else {
                        "\\b${Regex.escape(word)}\\b".toRegex()
                    }
            regex.containsMatchIn(message)
        }
    }

    /**
     * Find all highlight word matches in a message.
     * @param message The message to check
     * @param highlightWords Set of words/phrases to highlight
     * @param ignoreCase Whether to match case-insensitively (default true)
     * @return List of matched words (in original case from message)
     */
    fun findHighlights(
            message: String,
            highlightWords: Set<String>,
            ignoreCase: Boolean = true
    ): List<String> {
        if (highlightWords.isEmpty()) return emptyList()

        val matches = mutableListOf<String>()

        for (word in highlightWords) {
            if (word.isEmpty()) continue

            val regex =
                    if (ignoreCase) {
                        "\\b${Regex.escape(word)}\\b".toRegex(RegexOption.IGNORE_CASE)
                    } else {
                        "\\b${Regex.escape(word)}\\b".toRegex()
                    }

            regex.findAll(message).forEach { match -> matches.add(match.value) }
        }

        return matches.distinct()
    }

    /**
     * Check if a message mentions a specific nickname.
     * @param message The message to check
     * @param nickname The nickname to look for
     * @param ignoreCase Whether to match case-insensitively (default true)
     * @return true if the nickname is mentioned
     */
    fun containsMention(message: String, nickname: String, ignoreCase: Boolean = true): Boolean {
        if (nickname.isEmpty()) return false

        val regex =
                if (ignoreCase) {
                    "\\b${Regex.escape(nickname)}\\b".toRegex(RegexOption.IGNORE_CASE)
                } else {
                    "\\b${Regex.escape(nickname)}\\b".toRegex()
                }
        return regex.containsMatchIn(message)
    }
}
