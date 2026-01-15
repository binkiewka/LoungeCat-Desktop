package com.loungecat.irc.data.model

data class UrlPreview(
    val url: String,
    val title: String? = null,
    val description: String? = null,
    val imageUrl: String? = null,
    val siteName: String? = null,
    val favicon: String? = null,
    val isLoading: Boolean = false,
    val error: String? = null
)
