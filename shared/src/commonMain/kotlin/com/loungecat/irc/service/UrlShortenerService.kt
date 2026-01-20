package com.loungecat.irc.service

import com.loungecat.irc.util.Logger
import java.net.URI
import java.net.URLEncoder
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.charset.StandardCharsets
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

object UrlShortenerService {
    private const val API_URL = "https://is.gd/create.php?format=simple&url="
    private val client = HttpClient.newHttpClient()

    suspend fun shortenUrl(longUrl: String): String {
        return withContext(Dispatchers.IO) {
            try {
                val encodedUrl = URLEncoder.encode(longUrl, StandardCharsets.UTF_8)
                val requestUri = URI.create(API_URL + encodedUrl)

                val request = HttpRequest.newBuilder().uri(requestUri).GET().build()

                val response = client.send(request, HttpResponse.BodyHandlers.ofString())

                if (response.statusCode() == 200) {
                    val result = response.body().trim()
                    // is.gd returns "Error: ..." on failure even with 200 sometimes?
                    // Let's check if it looks like a URL.
                    if (result.startsWith("http")) {
                        return@withContext result
                    } else {
                        Logger.w("UrlShortenerService", "Shortening returned error: $result")
                        return@withContext longUrl
                    }
                } else {
                    Logger.w("UrlShortenerService", "Shortening failed: ${response.statusCode()}")
                    return@withContext longUrl
                }
            } catch (e: Exception) {
                Logger.e("UrlShortenerService", "Shortening exception", e)
                return@withContext longUrl
            }
        }
    }
}
