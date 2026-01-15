package com.loungecat.irc.service

import com.loungecat.irc.util.Logger
import java.net.URI
import java.net.http.HttpClient
import java.net.http.HttpRequest
import java.net.http.HttpResponse
import java.util.*
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Service for uploading long messages to a pastebin service. Uses 0x0.st as the default pastebin
 * provider (no API key required).
 */
object PastebinService {
    private const val PASTEBIN_URL = "https://0x0.st"
    private const val MAX_DIRECT_MESSAGE_LENGTH = 400
    private const val MAX_LINES_DIRECT = 5

    private val client = HttpClient.newHttpClient()

    /**
     * Check if a message should be pasted instead of sent directly.
     * @return true if message is too long or has too many lines
     */
    fun shouldPaste(message: String): Boolean {
        val lineCount = message.count { it == '\n' } + 1
        return message.length > MAX_DIRECT_MESSAGE_LENGTH || lineCount > MAX_LINES_DIRECT
    }

    /**
     * Upload text content to the pastebin service.
     * @param content The text to upload
     * @param filename Optional filename for the paste
     * @return The URL of the uploaded paste, or null on failure
     */
    suspend fun upload(content: String, filename: String = "paste.txt"): String? {
        return withContext(Dispatchers.IO) {
            try {
                val boundary = UUID.randomUUID().toString()
                val bodyBuilder = StringBuilder()

                // Build multipart form data manually
                bodyBuilder.append("--$boundary\r\n")
                bodyBuilder.append(
                        "Content-Disposition: form-data; name=\"file\"; filename=\"$filename\"\r\n"
                )
                bodyBuilder.append("Content-Type: text/plain\r\n\r\n")
                bodyBuilder.append(content)
                bodyBuilder.append("\r\n--$boundary--\r\n")

                val request =
                        HttpRequest.newBuilder()
                                .uri(URI.create(PASTEBIN_URL))
                                .header("Content-Type", "multipart/form-data; boundary=$boundary")
                                .POST(HttpRequest.BodyPublishers.ofString(bodyBuilder.toString()))
                                .build()

                val response = client.send(request, HttpResponse.BodyHandlers.ofString())

                if (response.statusCode() in 200..299) {
                    val url = response.body().trim()
                    Logger.d("PastebinService", "Uploaded paste successfully: $url")
                    url
                } else {
                    Logger.e(
                            "PastebinService",
                            "Upload failed with status: ${response.statusCode()}"
                    )
                    null
                }
            } catch (e: Exception) {
                Logger.e("PastebinService", "Failed to upload paste", e)
                null
            }
        }
    }

    /**
     * Upload content and return a formatted message with the paste URL.
     * @param content The text to upload
     * @param context Optional context message to prepend
     * @return A short message with the paste URL, or null on failure
     */
    suspend fun uploadAndFormat(content: String, context: String? = null): String? {
        val url = upload(content) ?: return null
        return if (context != null) {
            "$context: $url"
        } else {
            "Paste: $url"
        }
    }
}
