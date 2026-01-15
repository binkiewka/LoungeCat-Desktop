package com.loungecat.irc.util

object ImageUrlDetector {
    
    private val IMAGE_EXTENSIONS = setOf(
        "jpg", "jpeg", "png", "gif", "webp", "bmp", "svg",
        "ico", "tiff", "tif", "heic", "heif", "avif"
    )
    
    private val IMAGE_HOSTING_DOMAINS = setOf(
        "i.imgur.com",
        "imgur.com/i/",
        "i.redd.it",
        "i.reddituploads.com",
        "preview.redd.it",
        "external-preview.redd.it",
        "pbs.twimg.com",
        "media.discordapp.net",
        "cdn.discordapp.com",
        "media.tenor.com",
        "c.tenor.com",
        "media.giphy.com",
        "i.giphy.com",
        "media1.giphy.com",
        "media2.giphy.com",
        "media3.giphy.com",
        "media4.giphy.com",
        "i.ibb.co",
        "postimg.cc",
        "prnt.sc",
        "gyazo.com/",
        "steamusercontent.com",
        "steamcommunity.com/sharedfiles"
    )
    
    fun isImageUrl(url: String): Boolean {
        val lowerUrl = url.lowercase()
        return hasImageExtension(lowerUrl) || isFromImageHostingDomain(lowerUrl)
    }
    
    fun extractImageUrls(text: String): List<String> {
        return UrlExtractor.extractUrls(text).filter { isImageUrl(it) }
    }
    
    private fun hasImageExtension(url: String): Boolean {
        val urlWithoutParams = url.substringBefore('?').substringBefore('#')
        val extension = urlWithoutParams.substringAfterLast('.', "")
        return extension.isNotEmpty() && IMAGE_EXTENSIONS.contains(extension.lowercase())
    }
    
    private fun isFromImageHostingDomain(url: String): Boolean {
        return IMAGE_HOSTING_DOMAINS.any { domain -> url.contains(domain, ignoreCase = true) }
    }
    
    fun getDirectImageUrl(url: String): String {
        val lowerUrl = url.lowercase()
        
        if (lowerUrl.contains("imgur.com") && !lowerUrl.contains("i.imgur.com")) {
            val imageId = url.substringAfterLast("/").substringBefore(".")
            if (imageId.isNotEmpty() && !imageId.contains("gallery")) {
                return "https://i.imgur.com/$imageId.jpg"
            }
        }
        
        if (lowerUrl.contains("gyazo.com/") && !hasImageExtension(lowerUrl)) {
            return "$url.png"
        }
        
        return url
    }
}
