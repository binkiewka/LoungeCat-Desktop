package com.loungecat.irc.service

import com.loungecat.irc.BuildConfig
import com.loungecat.irc.data.model.GithubRelease
import com.loungecat.irc.data.model.UpdateResult
import java.net.HttpURLConnection
import java.net.URL
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json

class UpdateCheckService {
    private val json = Json { ignoreUnknownKeys = true }
    private val repoUrl = "https://api.github.com/repos/binkiewka/LoungeCat-Desktop/releases/latest"

    suspend fun checkForUpdates(): UpdateResult? =
            withContext(Dispatchers.IO) {
                try {
                    val url = URL(repoUrl)
                    val connection = url.openConnection() as HttpURLConnection
                    connection.requestMethod = "GET"
                    connection.connectTimeout = 5000
                    connection.readTimeout = 5000
                    connection.setRequestProperty("User-Agent", "LoungeCat-Desktop")

                    if (connection.responseCode == HttpURLConnection.HTTP_OK) {
                        val response = connection.inputStream.bufferedReader().use { it.readText() }
                        val release = json.decodeFromString<GithubRelease>(response)

                        val currentVersion = BuildConfig.VERSION.removePrefix("v")
                        val latestVersion = release.tagName.removePrefix("v")

                        if (isNewerVersion(currentVersion, latestVersion)) {
                            UpdateResult(
                                    hasUpdate = true,
                                    latestVersion = release.tagName,
                                    releaseUrl = release.htmlUrl
                            )
                        } else {
                            UpdateResult(
                                    hasUpdate = false,
                                    latestVersion = release.tagName,
                                    releaseUrl = release.htmlUrl
                            )
                        }
                    } else {
                        com.loungecat.irc.util.Logger.e(
                                "UpdateCheckService",
                                "Failed to check for updates: ${connection.responseCode}"
                        )
                        null
                    }
                } catch (e: Exception) {
                    com.loungecat.irc.util.Logger.e(
                            "UpdateCheckService",
                            "Error checking for updates",
                            e
                    )
                    null
                }
            }

    private fun isNewerVersion(current: String, latest: String): Boolean {
        // Simple semantic versioning check
        val currentParts = current.split(".").mapNotNull { it.toIntOrNull() }
        val latestParts = latest.split(".").mapNotNull { it.toIntOrNull() }

        val length = maxOf(currentParts.size, latestParts.size)

        for (i in 0 until length) {
            val v1 = currentParts.getOrElse(i) { 0 }
            val v2 = latestParts.getOrElse(i) { 0 }

            if (v1 < v2) return true
            if (v1 > v2) return false
        }

        return false
    }
}
