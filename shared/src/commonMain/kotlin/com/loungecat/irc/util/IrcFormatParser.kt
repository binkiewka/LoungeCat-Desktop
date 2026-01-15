package com.loungecat.irc.util

import androidx.compose.ui.graphics.Color

/** Parser for mIRC formatting codes. Handles bold, italic, underline, color codes, and reset. */
object IrcFormatParser {
    const val BOLD = '\u0002' // ^B
    const val ITALIC = '\u001D' // ^]
    const val UNDERLINE = '\u001F' // ^_
    const val COLOR = '\u0003' // ^C followed by fg[,bg]
    const val RESET = '\u000F' // ^O
    const val REVERSE = '\u0016' // ^V (reverse colors)
    const val STRIKETHROUGH = '\u001E' // ^~

    // mIRC 16-color palette
    val mircColors =
            listOf(
                    Color.White, // 0 - White
                    Color.Black, // 1 - Black
                    Color(0xFF00007F), // 2 - Navy Blue
                    Color(0xFF009300), // 3 - Green
                    Color(0xFFFF0000), // 4 - Red
                    Color(0xFF7F0000), // 5 - Brown/Maroon
                    Color(0xFF9C009C), // 6 - Purple
                    Color(0xFFFC7F00), // 7 - Orange
                    Color(0xFFFFFF00), // 8 - Yellow
                    Color(0xFF00FC00), // 9 - Light Green
                    Color(0xFF009393), // 10 - Cyan (Teal)
                    Color(0xFF00FFFF), // 11 - Light Cyan (Aqua)
                    Color(0xFF0000FC), // 12 - Light Blue
                    Color(0xFFFF00FF), // 13 - Pink (Fuchsia)
                    Color(0xFF7F7F7F), // 14 - Grey
                    Color(0xFFD2D2D2) // 15 - Light Grey
            )

    /** A segment of formatted text with style information. */
    data class FormattedSegment(
            val text: String,
            val bold: Boolean = false,
            val italic: Boolean = false,
            val underline: Boolean = false,
            val strikethrough: Boolean = false,
            val foregroundColor: Int? = null,
            val backgroundColor: Int? = null
    )

    /** Parse a string containing mIRC format codes into a list of formatted segments. */
    fun parse(text: String): List<FormattedSegment> {
        if (text.isEmpty()) return emptyList()

        val segments = mutableListOf<FormattedSegment>()
        var currentText = StringBuilder()
        var bold = false
        var italic = false
        var underline = false
        var strikethrough = false
        var fgColor: Int? = null
        var bgColor: Int? = null
        var i = 0

        fun flushSegment() {
            if (currentText.isNotEmpty()) {
                segments.add(
                        FormattedSegment(
                                text = currentText.toString(),
                                bold = bold,
                                italic = italic,
                                underline = underline,
                                strikethrough = strikethrough,
                                foregroundColor = fgColor,
                                backgroundColor = bgColor
                        )
                )
                currentText = StringBuilder()
            }
        }

        while (i < text.length) {
            when (text[i]) {
                BOLD -> {
                    flushSegment()
                    bold = !bold
                    i++
                }
                ITALIC -> {
                    flushSegment()
                    italic = !italic
                    i++
                }
                UNDERLINE -> {
                    flushSegment()
                    underline = !underline
                    i++
                }
                STRIKETHROUGH -> {
                    flushSegment()
                    strikethrough = !strikethrough
                    i++
                }
                REVERSE -> {
                    // Reverse video - swap fg/bg (we'll just toggle a flag)
                    flushSegment()
                    val temp = fgColor
                    fgColor = bgColor
                    bgColor = temp
                    i++
                }
                RESET -> {
                    flushSegment()
                    bold = false
                    italic = false
                    underline = false
                    strikethrough = false
                    fgColor = null
                    bgColor = null
                    i++
                }
                COLOR -> {
                    flushSegment()
                    i++ // Skip the color control character

                    // Parse foreground color (1 or 2 digits)
                    val fgStart = i
                    while (i < text.length && i < fgStart + 2 && text[i].isDigit()) {
                        i++
                    }

                    if (i > fgStart) {
                        val fg = text.substring(fgStart, i).toIntOrNull()
                        fgColor = fg?.let { if (it in 0..15) it else null }

                        // Check for background color
                        if (i < text.length && text[i] == ',') {
                            i++ // Skip comma
                            val bgStart = i
                            while (i < text.length && i < bgStart + 2 && text[i].isDigit()) {
                                i++
                            }
                            if (i > bgStart) {
                                val bg = text.substring(bgStart, i).toIntOrNull()
                                bgColor = bg?.let { if (it in 0..15) it else null }
                            }
                        }
                    } else {
                        // Color code without number resets colors
                        fgColor = null
                        bgColor = null
                    }
                }
                else -> {
                    currentText.append(text[i])
                    i++
                }
            }
        }

        flushSegment()
        return segments
    }

    /** Strip all formatting codes from a string, returning plain text. */
    fun stripFormatting(text: String): String {
        val result = StringBuilder()
        var i = 0

        while (i < text.length) {
            when (text[i]) {
                BOLD, ITALIC, UNDERLINE, STRIKETHROUGH, REVERSE, RESET -> i++
                COLOR -> {
                    i++ // Skip control character
                    // Skip foreground digits
                    while (i < text.length && i < text.length && text[i].isDigit()) {
                        i++
                    }
                    // Skip comma and background digits
                    if (i < text.length && text[i] == ',') {
                        i++
                        while (i < text.length && text[i].isDigit()) {
                            i++
                        }
                    }
                }
                else -> {
                    result.append(text[i])
                    i++
                }
            }
        }

        return result.toString()
    }

    /** Get the Color object for a mIRC color index. */
    fun getColor(colorIndex: Int): Color? {
        return if (colorIndex in 0..15) mircColors[colorIndex] else null
    }

    /** Insert a bold formatting code. */
    fun insertBold(text: String, position: Int): Pair<String, Int> {
        val newText = text.substring(0, position) + BOLD + text.substring(position)
        return Pair(newText, position + 1)
    }

    /** Insert an italic formatting code. */
    fun insertItalic(text: String, position: Int): Pair<String, Int> {
        val newText = text.substring(0, position) + ITALIC + text.substring(position)
        return Pair(newText, position + 1)
    }

    /** Insert an underline formatting code. */
    fun insertUnderline(text: String, position: Int): Pair<String, Int> {
        val newText = text.substring(0, position) + UNDERLINE + text.substring(position)
        return Pair(newText, position + 1)
    }
}
