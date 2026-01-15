package com.loungecat.irc.util

import java.awt.Desktop
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
        val home = System.getProperty("user.home")
        
        return when {
            os.contains("win") -> "$home\\AppData\\Local\\LoungeCat"
            os.contains("mac") -> "$home/Library/Application Support/LoungeCat"
            else -> "$home/.loungecat"
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
