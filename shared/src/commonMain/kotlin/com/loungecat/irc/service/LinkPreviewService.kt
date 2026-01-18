package com.loungecat.irc.service

import com.loungecat.irc.data.model.UrlPreview
import com.loungecat.irc.util.Logger
import java.net.Proxy
import java.net.URL
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.jsoup.Jsoup
import org.jsoup.nodes.Document

/** Service for fetching URL preview metadata */
class LinkPreviewService {

    companion object {
        private const val TIMEOUT_MS = 5000
        private const val USER_AGENT = "Mozilla/5.0 (compatible; LoungeCat IRC Client)"
        private const val MAX_DESCRIPTION_LENGTH = 200
    }

    /** Fetch preview metadata for a URL */
    suspend fun fetchPreview(url: String, proxy: java.net.Proxy? = null): UrlPreview =
            withContext(Dispatchers.IO) {
                try {
                    Logger.debug("LinkPreviewService", "Fetching preview for: $url")

                    // First, check if it's a direct image URL by extension
                    if (isDirectImageUrl(url)) {
                        Logger.debug(
                                "LinkPreviewService",
                                "Detected direct image URL, creating image-only preview"
                        )
                        return@withContext createImageOnlyPreview(url)
                    }

                    // Try to fetch with Jsoup, which will follow redirects
                    val connection =
                            Jsoup.connect(url)
                                    .userAgent(USER_AGENT)
                                    .timeout(TIMEOUT_MS)
                                    .followRedirects(true)
                                    .ignoreContentType(true)

                    if (proxy != null) {
                        connection.proxy(proxy)
                    }

                    val document = connection.get()

                    val title = extractTitle(document, url)
                    val description = extractDescription(document)
                    val imageUrl = extractImage(document, url)
                    val siteName = extractSiteName(document, url)
                    val favicon = extractFavicon(document, url)

                    UrlPreview(
                            url = url,
                            title = title,
                            description = description,
                            imageUrl = imageUrl,
                            siteName = siteName,
                            favicon = favicon
                    )
                } catch (e: Exception) {
                    Logger.error("LinkPreviewService", "Error fetching preview for $url", e)
                    UrlPreview(url = url, error = e.message)
                }
            }

    private fun isDirectImageUrl(url: String): Boolean {
        val lowerUrl = url.lowercase()
        return lowerUrl.endsWith(".jpg") ||
                lowerUrl.endsWith(".jpeg") ||
                lowerUrl.endsWith(".png") ||
                lowerUrl.endsWith(".gif") ||
                lowerUrl.endsWith(".webp") ||
                lowerUrl.endsWith(".bmp")
    }

    private fun createImageOnlyPreview(url: String): UrlPreview {
        return UrlPreview(
                url = url,
                title = url.substringAfterLast("/"),
                description = null,
                imageUrl = url,
                siteName = null,
                favicon = null
        )
    }
    /** Extract title from document */
    private fun extractTitle(document: Document, url: String): String? {
        // Try Open Graph title
        document.select("meta[property=og:title]").firstOrNull()?.attr("content")?.let {
            if (it.isNotBlank()) return it
        }

        // Try Twitter title
        document.select("meta[name=twitter:title]").firstOrNull()?.attr("content")?.let {
            if (it.isNotBlank()) return it
        }

        // Try regular title tag
        document.select("title").firstOrNull()?.text()?.let { if (it.isNotBlank()) return it }

        // Fallback to domain name
        return try {
            URL(url).host
        } catch (e: Exception) {
            null
        }
    }

    /** Extract description from document */
    private fun extractDescription(document: Document): String? {
        // Try Open Graph description
        document.select("meta[property=og:description]").firstOrNull()?.attr("content")?.let {
            if (it.isNotBlank()) return truncateDescription(it)
        }

        // Try Twitter description
        document.select("meta[name=twitter:description]").firstOrNull()?.attr("content")?.let {
            if (it.isNotBlank()) return truncateDescription(it)
        }

        // Try regular meta description
        document.select("meta[name=description]").firstOrNull()?.attr("content")?.let {
            if (it.isNotBlank()) return truncateDescription(it)
        }

        return null
    }

    /** Extract image from document */
    private fun extractImage(document: Document, baseUrl: String): String? {
        // Try Open Graph image
        document.select("meta[property=og:image]").firstOrNull()?.attr("content")?.let {
            if (it.isNotBlank()) return resolveUrl(it, baseUrl)
        }

        // Try Twitter image
        document.select("meta[name=twitter:image]").firstOrNull()?.attr("content")?.let {
            if (it.isNotBlank()) return resolveUrl(it, baseUrl)
        }

        // Try first image in content
        document.select("img[src]").firstOrNull()?.attr("src")?.let {
            if (it.isNotBlank()) return resolveUrl(it, baseUrl)
        }

        return null
    }

    /** Extract site name from document */
    private fun extractSiteName(document: Document, url: String): String? {
        // Try Open Graph site name
        document.select("meta[property=og:site_name]").firstOrNull()?.attr("content")?.let {
            if (it.isNotBlank()) return it
        }

        // Fallback to domain name
        return try {
            URL(url).host.removePrefix("www.")
        } catch (e: Exception) {
            null
        }
    }

    /** Extract favicon from document */
    private fun extractFavicon(document: Document, baseUrl: String): String? {
        val faviconSelectors =
                listOf("link[rel~=icon]", "link[rel~=shortcut icon]", "link[rel~=apple-touch-icon]")

        for (selector in faviconSelectors) {
            document.select(selector).firstOrNull()?.attr("href")?.let {
                if (it.isNotBlank()) return resolveUrl(it, baseUrl)
            }
        }

        // Fallback to /favicon.ico
        return try {
            val url = URL(baseUrl)
            "${url.protocol}://${url.host}/favicon.ico"
        } catch (e: Exception) {
            null
        }
    }

    /** Resolve relative URLs to absolute */
    private fun resolveUrl(url: String, baseUrl: String): String {
        return try {
            if (url.startsWith("http://") || url.startsWith("https://")) {
                url
            } else if (url.startsWith("//")) {
                "https:$url"
            } else {
                val base = URL(baseUrl)
                URL(base, url).toString()
            }
        } catch (e: Exception) {
            url
        }
    }

    /** Truncate description to max length */
    private fun truncateDescription(description: String): String {
        return if (description.length > MAX_DESCRIPTION_LENGTH) {
            description.take(MAX_DESCRIPTION_LENGTH - 3) + "..."
        } else {
            description
        }
    }
}
