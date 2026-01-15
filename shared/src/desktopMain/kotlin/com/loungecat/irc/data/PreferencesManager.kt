package com.loungecat.irc.data

import com.loungecat.irc.data.model.UserPreferences
import com.loungecat.irc.util.Logger
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json
import java.io.File

object PreferencesManager {
    private val json = Json {
        prettyPrint = true
        ignoreUnknownKeys = true
    }

    private var preferencesFile: File? = null
    private var currentPreferences: UserPreferences = UserPreferences()

    fun initialize(configFile: File) {
        preferencesFile = configFile
        loadPreferences()
    }

    private fun loadPreferences() {
        try {
            preferencesFile?.let { file ->
                if (file.exists()) {
                    val content = file.readText()
                    currentPreferences = json.decodeFromString<UserPreferences>(content)
                    Logger.d("PreferencesManager", "Loaded preferences from ${file.absolutePath}")
                } else {
                    savePreferences() // Create default preferences file
                    Logger.d("PreferencesManager", "Created default preferences at ${file.absolutePath}")
                }
            }
        } catch (e: Exception) {
            Logger.e("PreferencesManager", "Failed to load preferences, using defaults", e)
            currentPreferences = UserPreferences()
        }
    }

    fun savePreferences() {
        try {
            preferencesFile?.let { file ->
                file.parentFile?.mkdirs()
                val content = json.encodeToString(currentPreferences)
                file.writeText(content)
                Logger.d("PreferencesManager", "Saved preferences")
            }
        } catch (e: Exception) {
            Logger.e("PreferencesManager", "Failed to save preferences", e)
        }
    }

    fun getPreferences(): UserPreferences = currentPreferences

    fun updatePreferences(updater: (UserPreferences) -> UserPreferences) {
        currentPreferences = updater(currentPreferences)
        savePreferences()
    }
}
