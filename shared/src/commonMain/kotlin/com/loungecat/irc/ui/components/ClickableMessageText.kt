package com.loungecat.irc.ui.components

// LocalUriHandler removed as LinkAnnotation handles it
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.TooltipArea
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.LinkAnnotation
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextLinkStyles
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.text.withLink
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.TextUnit
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.loungecat.irc.ui.theme.ThemeColors
import com.loungecat.irc.util.IrcFormatParser
import com.loungecat.irc.util.UrlExtractor

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ClickableMessageText(
        text: String,
        colors: ThemeColors,
        textColor: Color,
        modifier: Modifier = Modifier,
        nickname: String? = null,
        nicknameColor: Color? = null,
        fontSize: TextUnit = 12.sp,
        whoisInfo: String? = null,
        onRequestWhois: (() -> Unit)? = null
) {
    val annotatedString =
            remember(text, nickname, nicknameColor) {
                buildAnnotatedString {
                    // First, parse the text for IRC formatting
                    val formattedSegments = IrcFormatParser.parse(text)

                    // Build plain text for URL extraction (strip formatting)
                    val plainText = IrcFormatParser.stripFormatting(text)
                    val urls = UrlExtractor.extractUrls(plainText)

                    // Track position in plain text for URL matching
                    var plainTextIndex = 0

                    for (segment in formattedSegments) {
                        val segmentStart = plainTextIndex
                        val segmentEnd = plainTextIndex + segment.text.length

                        // Calculate base style from IRC formatting
                        val baseStyle =
                                SpanStyle(
                                        color =
                                                segment.foregroundColor?.let {
                                                    IrcFormatParser.getColor(it)
                                                }
                                                        ?: textColor,
                                        fontWeight = if (segment.bold) FontWeight.Bold else null,
                                        fontStyle = if (segment.italic) FontStyle.Italic else null,
                                        textDecoration =
                                                when {
                                                    segment.underline && segment.strikethrough ->
                                                            TextDecoration.combine(
                                                                    listOf(
                                                                            TextDecoration
                                                                                    .Underline,
                                                                            TextDecoration
                                                                                    .LineThrough
                                                                    )
                                                            )
                                                    segment.underline -> TextDecoration.Underline
                                                    segment.strikethrough ->
                                                            TextDecoration.LineThrough
                                                    else -> null
                                                },
                                        background =
                                                segment.backgroundColor?.let {
                                                    IrcFormatParser.getColor(it)
                                                            ?: Color.Transparent
                                                }
                                                        ?: Color.Transparent
                                )

                        // Process the segment text, looking for URLs and nicknames
                        var currentPos = 0
                        val segmentText = segment.text

                        while (currentPos < segmentText.length) {
                            var nextSpecialStart = segmentText.length
                            var nextSpecialType: String? = null
                            var nextSpecialContent: String? = null

                            // Check for nickname in this segment
                            if (nickname != null && nicknameColor != null) {
                                val nickIdx = segmentText.indexOf(nickname, currentPos)
                                if (nickIdx >= 0 && nickIdx < nextSpecialStart) {
                                    nextSpecialStart = nickIdx
                                    nextSpecialType = "nickname"
                                    nextSpecialContent = nickname
                                }
                            }

                            // Check for URLs in this segment
                            for (url in urls) {
                                val originalUrl = findOriginalUrl(segmentText, url, currentPos)
                                if (originalUrl != null) {
                                    val urlIdx = segmentText.indexOf(originalUrl, currentPos)
                                    if (urlIdx >= 0 && urlIdx < nextSpecialStart) {
                                        nextSpecialStart = urlIdx
                                        nextSpecialType = "url"
                                        nextSpecialContent = originalUrl
                                    }
                                }
                            }

                            // Add text before the special content
                            if (nextSpecialStart > currentPos) {
                                withStyle(style = baseStyle) {
                                    append(segmentText.substring(currentPos, nextSpecialStart))
                                }
                            }

                            // Handle the special content
                            when (nextSpecialType) {
                                "nickname" -> {
                                    withStyle(
                                            style =
                                                    baseStyle.copy(
                                                            color = nicknameColor!!,
                                                            fontWeight = FontWeight.Bold
                                                    )
                                    ) { append(nextSpecialContent!!) }
                                    currentPos = nextSpecialStart + nextSpecialContent!!.length
                                }
                                "url" -> {
                                    val url =
                                            urls.find {
                                                findOriginalUrl(
                                                        segmentText,
                                                        it,
                                                        nextSpecialStart
                                                ) == nextSpecialContent
                                            }
                                                    ?: nextSpecialContent!!

                                    val link =
                                            LinkAnnotation.Url(
                                                    url = url,
                                                    styles =
                                                            TextLinkStyles(
                                                                    style =
                                                                            baseStyle.copy(
                                                                                    color =
                                                                                            colors.linkColor,
                                                                                    textDecoration =
                                                                                            TextDecoration
                                                                                                    .Underline
                                                                            )
                                                            )
                                            )
                                    withLink(link) { append(nextSpecialContent!!) }
                                    currentPos = nextSpecialStart + nextSpecialContent!!.length
                                }
                                else -> {
                                    currentPos = segmentText.length
                                }
                            }
                        }

                        plainTextIndex = segmentEnd
                    }
                }
            }

    // Show tooltip if nickname is present (for WHOIS info or trigger request)
    if (nickname != null) {
        // Trigger WHOIS request when we don't have cached info
        if (whoisInfo == null && onRequestWhois != null) {
            var hasRequestedWhois by remember { mutableStateOf(false) }

            TooltipArea(
                    tooltip = {
                        Text(
                                text = "Loading WHOIS for $nickname...",
                                color = colors.foreground,
                                fontSize = 12.sp,
                                modifier =
                                        Modifier.shadow(4.dp, RoundedCornerShape(4.dp))
                                                .background(
                                                        colors.windowBackground.copy(alpha = 0.95f),
                                                        RoundedCornerShape(4.dp)
                                                )
                                                .padding(8.dp)
                        )
                        // Request WHOIS on first hover
                        LaunchedEffect(nickname) {
                            if (!hasRequestedWhois) {
                                hasRequestedWhois = true
                                onRequestWhois()
                            }
                        }
                    },
                    delayMillis = 600
            ) {
                Text(
                        text = annotatedString,
                        style = MaterialTheme.typography.bodySmall.copy(fontSize = fontSize),
                        modifier = modifier
                )
            }
        } else if (whoisInfo != null) {
            TooltipArea(
                    tooltip = {
                        Text(
                                text = whoisInfo,
                                color = colors.foreground,
                                fontSize = 12.sp,
                                modifier =
                                        Modifier.shadow(4.dp, RoundedCornerShape(4.dp))
                                                .background(
                                                        colors.windowBackground.copy(alpha = 0.95f),
                                                        RoundedCornerShape(4.dp)
                                                )
                                                .padding(8.dp)
                        )
                    },
                    delayMillis = 400
            ) {
                Text(
                        text = annotatedString,
                        style = MaterialTheme.typography.bodySmall.copy(fontSize = fontSize),
                        modifier = modifier
                )
            }
        } else {
            Text(
                    text = annotatedString,
                    style = MaterialTheme.typography.bodySmall.copy(fontSize = fontSize),
                    modifier = modifier
            )
        }
    } else {
        Text(
                text = annotatedString,
                style = MaterialTheme.typography.bodySmall.copy(fontSize = fontSize),
                modifier = modifier
        )
    }
}

private fun findOriginalUrl(text: String, normalizedUrl: String, startFrom: Int): String? {
    val urlWithoutProtocol = normalizedUrl.removePrefix("https://").removePrefix("http://")

    val variations =
            listOf(
                    normalizedUrl,
                    urlWithoutProtocol,
                    "www.$urlWithoutProtocol",
                    "http://$urlWithoutProtocol",
                    "https://$urlWithoutProtocol"
            )

    for (variation in variations) {
        val index = text.indexOf(variation, startFrom)
        if (index >= 0) {
            return variation
        }
    }

    val index = text.indexOf(urlWithoutProtocol, startFrom)
    if (index >= 0) {
        var endIndex = index + urlWithoutProtocol.length
        while (endIndex < text.length && !text[endIndex].isWhitespace()) {
            endIndex++
        }
        return text.substring(index, endIndex)
    }

    return null
}
