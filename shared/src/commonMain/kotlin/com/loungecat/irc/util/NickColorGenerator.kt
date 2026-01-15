package com.loungecat.irc.util

import androidx.compose.ui.graphics.Color

object NickColorGenerator {

    private val darkColorPalette =
            listOf(
                    Color(0xFFFF5555),
                    Color(0xFFFF79C6),
                    Color(0xFFBD93F9),
                    Color(0xFF8BE9FD),
                    Color(0xFF50FA7B),
                    Color(0xFFF1FA8C),
                    Color(0xFFFFB86C),
                    Color(0xFFFF6E67),
                    Color(0xFF95E1D3),
                    Color(0xFFF38181),
                    Color(0xFFAA96DA),
                    Color(0xFFFCBAD3),
                    Color(0xFFFFDEE9),
                    Color(0xFFA8E6CF),
                    Color(0xFFFFD3B6),
                    Color(0xFFFFAAA5)
            )

    // Darker colors for light theme to ensure contrast
    private val lightColorPalette =
            listOf(
                    Color(0xFFD32F2F), // Red
                    Color(0xFFC2185B), // Pink
                    Color(0xFF7B1FA2), // Purple
                    Color(0xFF0097A7), // Cyan
                    Color(0xFF388E3C), // Green
                    Color(0xFFF57C00), // Orange (Yellow replacement)
                    Color(0xFFE64A19), // Deep Orange
                    Color(0xFF5D4037), // Brown
                    Color(0xFF0288D1), // Light Blue
                    Color(0xFF455A64), // Blue Grey
                    Color(0xFF689F38), // Light Green
                    Color(0xFFFFA000), // Amber
                    Color(0xFF512DA8), // Deep Purple
                    Color(0xFF00796B), // Teal
                    Color(0xFFC62828), // Red
                    Color(0xFF2E7D32) // Green
            )

    private val colorCache = mutableMapOf<String, Color>()

    fun getColorForNickname(nickname: String, isDarkTheme: Boolean = true): Color {
        // Cache key must include theme mode to avoid wrong colors when switching
        val cacheKey = "$nickname-$isDarkTheme"
        colorCache[cacheKey]?.let {
            return it
        }

        val palette = if (isDarkTheme) darkColorPalette else lightColorPalette
        val hash = nickname.hashCode()
        val index = hash.abs() % palette.size
        val color = palette[index]

        colorCache[cacheKey] = color
        return color
    }

    fun clearCache() {
        colorCache.clear()
    }

    fun getPaletteSize(): Int = darkColorPalette.size

    private fun Int.abs(): Int = if (this >= 0) this else -this
}
