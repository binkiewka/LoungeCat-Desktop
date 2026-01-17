package com.loungecat.irc.data.model

import kotlinx.serialization.Serializable

@Serializable
enum class FontSize(val scaleFactor: Float) {
    SMALL(0.85f),
    MEDIUM(1.0f),
    LARGE(1.15f)
}

@Serializable
enum class TimestampFormat(val pattern: String) {
    HOURS_12("hh:mm a"),
    HOURS_24("HH:mm"),
    RELATIVE("relative")
}

@Serializable
enum class UrlImageDisplayMode {
    INLINE, // Always show previews inline
    COMPACT, // Show compact preview, expand on click
    DISABLED // No preview display
}

@Serializable
enum class JoinPartQuitDisplayMode {
    SHOW_ALL, // Show all join/part/quit messages (default)
    HIDE_ALL, // Hide all join/part/quit messages
    GROUPED, // Group consecutive messages into a single line
    SMART_HIDE // Only show if user was recently active
}

@Serializable
data class UserPreferences(
        val fontSize: FontSize = FontSize.MEDIUM,
        val timestampFormat: TimestampFormat = TimestampFormat.HOURS_24,
        val urlImageDisplayMode: UrlImageDisplayMode = UrlImageDisplayMode.INLINE,
        val coloredNicknames: Boolean = true,
        // Join/Part/Quit display options
        val joinPartQuitMode: JoinPartQuitDisplayMode = JoinPartQuitDisplayMode.SHOW_ALL,
        val smartHideMinutes: Int = 10, // Threshold for smart hide (in minutes)
        val ignoredUsers: Set<String> = emptySet(),
        // Batch 1: Activity Indicator
        val activityIndicatorMinutes: Int = 5,
        // Batch 1: Timezone
        val timezone: String? = null, // null = system default
        // Batch 2: Spell Check
        val spellCheckEnabled: Boolean = true,
        val spellCheckLanguage: String = "en_US",
        // Batch 2: Themes
        val theme: String = "Dark",
        // Batch 2: Pastebin
        val pastebinEnabled: Boolean = true,
        val pastebinThreshold: Int = 400, // characters
        // Batch 3: Notifications & Highlights
        val highlightWords: Set<String> = emptySet(),
        val notificationsEnabled: Boolean = true,
        val notifyOnMention: Boolean = true,
        val notifyOnHighlight: Boolean = true,
        val notifyOnPrivateMessage: Boolean = true,
        // Batch 3: Sound Alerts
        val soundAlertsEnabled: Boolean = false,
        val soundOnMention: Boolean = true,
        val soundOnHighlight: Boolean = true,
        val soundOnPrivateMessage: Boolean = true,
        val soundVolume: Float = 0.7f,
        // Batch 5: Split View
        val splitViewEnabled: Boolean = false,
        // Input History
        val inputHistorySize: Int = 50,
        // Logging & Replay
        val loggingEnabled: Boolean = false,
        val historyReplayLines: Int = 50,
)
