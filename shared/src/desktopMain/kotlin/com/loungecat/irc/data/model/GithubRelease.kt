package com.loungecat.irc.data.model

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

@Serializable
data class GithubRelease(
        @SerialName("tag_name") val tagName: String,
        @SerialName("html_url") val htmlUrl: String,
        val name: String,
        val body: String,
        val draft: Boolean,
        val prerelease: Boolean
)

data class UpdateResult(val hasUpdate: Boolean, val latestVersion: String, val releaseUrl: String)
