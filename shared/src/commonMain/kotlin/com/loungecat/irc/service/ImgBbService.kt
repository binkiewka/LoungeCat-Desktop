package com.loungecat.irc.service

import com.loungecat.irc.util.Logger
import java.net.URI
import java.net.URLEncoder
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.nio.charset.StandardCharsets
import java.util.Base64
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

object ImgBbService {
    private const val UPLOAD_URL = "https://api.imgbb.com/1/upload"
    // Using a shared client or new one? PastebinService uses new one.
    private val client = HttpClient.newHttpClient()

    suspend fun uploadImage(apiKey: String, imageBytes: ByteArray, name: String? = null): String? {
        return withContext(Dispatchers.IO) {
            try {
                Logger.d("ImgBbService", "Uploading image to ImgBB...")
                val base64Image = Base64.getEncoder().encodeToString(imageBytes)
                val encodedImage = URLEncoder.encode(base64Image, StandardCharsets.UTF_8)

                val bodyBuilder = StringBuilder()
                bodyBuilder.append("key=").append(apiKey)
                bodyBuilder.append("&image=").append(encodedImage)
                if (name != null) {
                    bodyBuilder
                            .append("&name=")
                            .append(URLEncoder.encode(name, StandardCharsets.UTF_8))
                }

                val request =
                        HttpRequest.newBuilder()
                                .uri(URI.create(UPLOAD_URL))
                                .header("Content-Type", "application/x-www-form-urlencoded")
                                .POST(HttpRequest.BodyPublishers.ofString(bodyBuilder.toString()))
                                .build()

                val response = client.send(request, HttpResponse.BodyHandlers.ofString())

                if (response.statusCode() == 200) {
                    val json = response.body()
                    // Simple regex extraction to avoid pulling in serialization deps if not needed
                    // for this one file
                    val urlMatch = "\"url\":\"(.*?)\"".toRegex().find(json)
                    // JSON strings might have escaped slashes
                    val url = urlMatch?.groupValues?.get(1)?.replace("\\/", "/")
                    Logger.d("ImgBbService", "Upload successful: $url")
                    return@withContext url
                } else {
                    Logger.e(
                            "ImgBbService",
                            "Upload failed: ${response.statusCode()} - ${response.body()}"
                    )
                    return@withContext null
                }
            } catch (e: Exception) {
                Logger.e("ImgBbService", "Upload exception", e)
                return@withContext null
            }
        }
    }
}
