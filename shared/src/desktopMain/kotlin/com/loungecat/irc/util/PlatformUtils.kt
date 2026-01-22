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

            // Check for migration marker to avoid re-running or overwriting verified data
            val migrationMarker = File(newDir, ".migration_complete_v1")
            if (migrationMarker.exists()) {
                Logger.d("PlatformUtils", "Migration already completed (marker exists)")
                return
            }

            // Identify source: Priority 1: ~/.loungecat, Priority 2: AppData/Local/LoungeCat
            val homeDir = File(System.getProperty("user.home"), ".loungecat")
            val legacyLocalDir = File(getLegacyAppDataDirectory())

            val sourceDir =
                    when {
                        homeDir.exists() && homeDir.isDirectory -> homeDir
                        legacyLocalDir.exists() && legacyLocalDir.isDirectory -> legacyLocalDir
                        else -> null
                    }

            if (sourceDir == null) {
                Logger.d("PlatformUtils", "No legacy data found to migrate")
                // Mark as migrated so we don't keep checking
                if (!newDir.exists()) newDir.mkdirs()
                migrationMarker.createNewFile()
                return
            }

            Logger.i("PlatformUtils", "Detecting Windows migration needed...")
            Logger.i("PlatformUtils", "Source: ${sourceDir.absolutePath}")
            Logger.i("PlatformUtils", "Target: ${newDir.absolutePath}")

            // Safety check: if target exists and has data (e.g. user generated new data in broken
            // version),
            // we have a conflict. For now, if the new DB is tiny (empty) or doesn't exist, we
            // overwrite.
            // If it seems used, we might back it up.

            if (!newDir.exists()) {
                newDir.mkdirs()
            }

            val targetDb = File(newDir, "loungecat.db")
            val targetPrefs = File(newDir, "preferences.json")

            // Heuristic: If target DB exists but is very small (< 20KB), it's likely an empty
            // default DB
            // created by the broken installer. We can safely overwrite it with the legacy user
            // data.
            val isTargetFresh = !targetDb.exists() || targetDb.length() < 20 * 1024 // 20KB

            if (isTargetFresh) {
                try {
                    // Copy specific files/folders to avoid clutter

                    // 1. Preferences
                    val srcPrefs = File(sourceDir, "preferences.json")
                    if (srcPrefs.exists()) {
                        srcPrefs.copyTo(targetPrefs, overwrite = true)
                        Logger.i("PlatformUtils", "Migrated preferences.json")
                    }

                    // 2. Database
                    val srcDb = File(sourceDir, "loungecat.db")
                    if (srcDb.exists()) {
                        srcDb.copyTo(targetDb, overwrite = true)
                        Logger.i("PlatformUtils", "Migrated loungecat.db")
                    }

                    // 3. Logs
                    val srcLogs = File(sourceDir, "logs")
                    if (srcLogs.exists()) {
                        val targetLogs = File(newDir, "logs")
                        srcLogs.copyRecursively(targetLogs, overwrite = true)
                        Logger.i("PlatformUtils", "Migrated logs")
                    }

                    // 4. Themes
                    val srcThemes = File(sourceDir, "themes")
                    if (srcThemes.exists()) {
                        val targetThemes = File(newDir, "themes")
                        srcThemes.copyRecursively(targetThemes, overwrite = true)
                        Logger.i("PlatformUtils", "Migrated themes")
                    }

                    // 5. Message Cache (Attachments/History) if it was file-based in that dir
                    // (Assuming standard structure provided in Main.kt previously)

                    // Mark completion
                    migrationMarker.createNewFile()
                    Logger.i("PlatformUtils", "Migration completed successfully")
                } catch (e: Exception) {
                    Logger.e("PlatformUtils", "Error during migration copy", e)
                }
            } else {
                Logger.w(
                        "PlatformUtils",
                        "Target directory has significant data, skipping migration to avoid data loss."
                )
                // Create marker to stop annoying the log, assuming user accepts current state?
                // Or leave it to try again if they delete the bad data?
                // Leaving it alone is safer.
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
