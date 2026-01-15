package com.loungecat.irc.ui.theme

import com.loungecat.irc.util.Logger
import java.io.File
import kotlinx.serialization.json.Json

object ThemeManager {
    private val json = Json {
        prettyPrint = true
        ignoreUnknownKeys = true
    }

    private var themesDir: File? = null
    private val loadedThemes = mutableMapOf<String, CustomTheme>()

    fun initialize(appDataDirPath: String) {
        themesDir = File(File(appDataDirPath), "themes")
        if (!themesDir!!.exists()) {
            themesDir!!.mkdirs()
            createTemplate()
        }
        loadThemes()
    }

    fun loadThemes() {
        loadedThemes.clear()
        themesDir
                ?.listFiles { file -> file.extension == "json" && !file.name.startsWith("_") }
                ?.forEach { file ->
                    try {
                        val content = file.readText()
                        val theme = json.decodeFromString<CustomTheme>(content)
                        loadedThemes[theme.name] = theme
                        Logger.d("ThemeManager", "Loaded theme: ${theme.name}")
                    } catch (e: Exception) {
                        Logger.e("ThemeManager", "Failed to load theme from ${file.name}", e)
                    }
                }
    }

    fun getTheme(name: String): ThemeColors {
        // First check built-in enum
        val builtIn = AppTheme.entries.find { it.name.equals(name, ignoreCase = true) }
        if (builtIn != null) {
            return getThemeColors(builtIn)
        }

        // Then check loaded custom themes
        val customTheme = loadedThemes[name]
        if (customTheme != null) {
            return ThemeMapper.toThemeColors(customTheme)
        }

        // Fallback to Dark
        return getThemeColors(AppTheme.DARK)
    }

    fun getAvailableThemes(): List<String> {
        val builtIn =
                AppTheme.entries.map {
                    it.name.lowercase().replaceFirstChar { char -> char.uppercase() }
                }
        val custom = loadedThemes.keys.sorted()
        return builtIn + custom
    }

    fun openThemesFolder() {
        try {
            if (java.awt.Desktop.isDesktopSupported()) {
                val desktop = java.awt.Desktop.getDesktop()
                themesDir?.let { desktop.open(it) }
            }
        } catch (e: Exception) {
            Logger.e("ThemeManager", "Failed to open themes folder", e)
        }
    }

    private fun createTemplate() {
        val template =
                CustomTheme(
                        name = "My Custom Theme",
                        isDark = true,
                        colors =
                                mapOf(
                                        "background" to "#141414",
                                        "windowBackground" to "#0A0A0A",
                                        "currentLine" to "#252525",
                                        "selection" to "#353535",
                                        "foreground" to "#E0E0E0",
                                        "comment" to "#707070",
                                        "cyan" to "#80DEEA",
                                        "green" to "#81C784",
                                        "orange" to "#FFB74D",
                                        "pink" to "#F06292",
                                        "purple" to "#90CAF9",
                                        "red" to "#E57373",
                                        "yellow" to "#FFF176",
                                        "border" to "#252525",
                                        "timeColor" to "#707070",
                                        "msgSelf" to "#80DEEA",
                                        "msgAction" to "#FFF176",
                                        "linkColor" to "#80DEEA",
                                        "buttonColor" to "#81C784",
                                        "unreadMarker" to "#F06292",
                                        "dateMarker" to "#90CAF9",
                                        "highlightBg" to "#14FFFFFF",
                                        "chatBubbleBg" to "#1E1E1E",
                                        "formBackground" to "#101010",
                                        "errorColor" to "#E57373"
                                )
                )

        try {
            val file = File(themesDir, "_template.json")
            val content = json.encodeToString(CustomTheme.serializer(), template)
            // Add some comments to the JSON file explaining how to use it contextually isn't easy
            // with JSON lib directly,
            // but we can rely on the user understanding JSON.
            // Or we could write a README.txt
            file.writeText(content)

            File(themesDir, "README.txt")
                    .writeText(
                            """
                To create a custom theme:
                1. Copy _template.json and rename it (e.g., my_theme.json).
                2. Edit the "name" field to verify the name of your theme.
                3. Change the hex color codes to your liking.
                4. Restart the application or reload themes to see it in the settings.
            """.trimIndent()
                    )
        } catch (e: Exception) {
            Logger.e("ThemeManager", "Failed to create template", e)
        }
    }
}
