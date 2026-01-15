package com.loungecat.irc.util

import java.util.regex.Pattern

object UrlExtractor {
    
    private val URL_PATTERN = Pattern.compile(
        "(?:^|[\\W])((ht|f)tp(s?)://|www\\.)" +
        "(([\\w\\-]+\\.)+([\\w\\-\\.]+/?)+)" +
        "(\\?([\\w\\-\\.]+=[\\w\\-\\.]+&?)*)?" +
        "(#([\\w\\-\\.]+))?",
        Pattern.CASE_INSENSITIVE or Pattern.MULTILINE or Pattern.DOTALL
    )
    
    fun extractUrls(text: String): List<String> {
        val urls = mutableListOf<String>()
        val matcher = URL_PATTERN.matcher(text)
        
        while (matcher.find()) {
            var url = matcher.group().trim()
            url = url.trimStart { !it.isLetterOrDigit() }
            
            if (!url.startsWith("http://") && !url.startsWith("https://")) {
                url = "https://$url"
            }
            
            urls.add(url)
        }
        
        return urls.distinct()
    }
    
    fun containsUrl(text: String): Boolean = URL_PATTERN.matcher(text).find()
}
