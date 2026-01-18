package com.loungecat.irc.util

import java.awt.Desktop
import java.io.File
import java.net.URI

actual fun openUrl(url: String) {
    try {
        if (Desktop.isDesktopSupported()) {
            Desktop.getDesktop().browse(URI(url))
        }
    } catch (e: Exception) {
        Logger.e("PlatformUtils", "Failed to open URL: $url", e)
    }
}

object PlatformUtils {

    fun getAppDataDirectory(): String {
        val os = System.getProperty("os.name").lowercase()
        val home = System.getProperty("user.home").replace("\\", "/")

        val path =
                when {
                    os.contains("win") -> {
                        // Use Roaming AppData for Windows to persist settings across updates
                        val appData = System.getenv("APPDATA")?.replace("\\", "/")
                        if (appData != null) "$appData/LoungeCat"
                        else "$home/AppData/Roaming/LoungeCat"
                    }
                    os.contains("mac") -> "$home/Library/Application Support/LoungeCat"
                    else -> "$home/.loungecat"
                }

        // Log this early so we catch it in the debug log
        Logger.d("PlatformUtils", "Resolved AppData Path: $path (OS: $os, Home: $home)")
        return path
    }

    private fun getLegacyAppDataDirectory(): String {
        val home = System.getProperty("user.home").replace("\\", "/")
        return "$home/AppData/Local/LoungeCat"
    }

    fun performWindowsMigration() {
        try {
            val os = System.getProperty("os.name").lowercase()
            if (!os.contains("win")) return

            val newDir = File(getAppDataDirectory())
            val oldDir = File(getLegacyAppDataDirectory())

            // Only migrate if new dir doesn't exist (or is empty) and old dir exists
            if (!newDir.exists() && oldDir.exists()) {
                Logger.i("PlatformUtils", "Detecting Windows migration needed...")
                Logger.i(
                        "PlatformUtils",
                        "Migrating from ${oldDir.absolutePath} to ${newDir.absolutePath}"
                )

                try {
                    // Create new directory
                    if (newDir.mkdirs()) {
                        // Copy preferences
                        val oldPrefs = File(oldDir, "preferences.json")
                        if (oldPrefs.exists()) {
                            oldPrefs.copyTo(File(newDir, "preferences.json"))
                            Logger.i("PlatformUtils", "Migrated preferences.json")
                        }

                        // Copy database
                        val oldDb = File(oldDir, "loungecat.db")
                        if (oldDb.exists()) {
                            oldDb.copyTo(File(newDir, "loungecat.db"))
                            Logger.i("PlatformUtils", "Migrated loungecat.db")
                        }

                        // Copy logs folder if it exists
                        val oldLogs = File(oldDir, "logs")
                        if (oldLogs.exists() && oldLogs.isDirectory) {
                            oldLogs.copyRecursively(File(newDir, "logs"))
                            Logger.i("PlatformUtils", "Migrated logs directory")
                        }

                        // Copy custom themes folder if it exists
                        val oldThemes = File(oldDir, "themes")
                        if (oldThemes.exists() && oldThemes.isDirectory) {
                            oldThemes.copyRecursively(File(newDir, "themes"))
                            Logger.i("PlatformUtils", "Migrated themes directory")
                        }

                        Logger.i("PlatformUtils", "Migration completed successfully")
                    } else {
                        Logger.e(
                                "PlatformUtils",
                                "Failed to create new config directory: ${newDir.absolutePath}"
                        )
                    }
                } catch (e: Exception) {
                    Logger.e("PlatformUtils", "Error during migration: ${e.message}", e)
                }
            }
        } catch (e: Exception) {
            Logger.e("PlatformUtils", "Migration check failed", e)
        }
    }

    fun getDatabasePath(): String {
        val envPath = System.getenv("LOUNGECAT_DB_PATH")
        return if (!envPath.isNullOrBlank()) {
            envPath
        } else {
            "${getAppDataDirectory()}/loungecat.db"
        }
    }

    fun getConfigPath(): String {
        val envPath = System.getenv("LOUNGECAT_CONFIG")
        return if (!envPath.isNullOrBlank()) {
            envPath
        } else {
            "${getAppDataDirectory()}/preferences.json"
        }
    }

    fun getPreferencesPath(): String = "${getAppDataDirectory()}/settings.preferences_pb"
}
