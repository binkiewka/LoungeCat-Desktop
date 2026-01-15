package com.loungecat.irc.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.loungecat.irc.data.model.*
import com.loungecat.irc.ui.theme.AppColors
import com.loungecat.irc.util.NickColorGenerator
import java.text.SimpleDateFormat
import java.util.*

@Composable
fun MessageItem(
        message: IncomingMessage,
        urlPreviews: List<UrlPreview> = emptyList(),
        imageUrls: List<String> = emptyList(),
        currentNickname: String = "",
        urlImageDisplayMode: UrlImageDisplayMode = UrlImageDisplayMode.INLINE,
        fontSize: FontSize = FontSize.MEDIUM,
        timestampFormat: TimestampFormat = TimestampFormat.HOURS_24,
        coloredNicknames: Boolean = true
) {
    val colors = AppColors.current

    // Format timestamp based on user preference
    val formattedTime =
            remember(message.timestamp, timestampFormat) {
                when (timestampFormat) {
                    TimestampFormat.HOURS_12 ->
                            SimpleDateFormat("hh:mm a", Locale.getDefault())
                                    .format(Date(message.timestamp))
                    TimestampFormat.HOURS_24 ->
                            SimpleDateFormat("HH:mm", Locale.getDefault())
                                    .format(Date(message.timestamp))
                    TimestampFormat.RELATIVE -> {
                        val now = System.currentTimeMillis()
                        val diff = now - message.timestamp
                        when {
                            diff < 60_000 -> "now"
                            diff < 3600_000 -> "${diff / 60_000}m"
                            diff < 86400_000 -> "${diff / 3600_000}h"
                            else ->
                                    SimpleDateFormat("MM/dd", Locale.getDefault())
                                            .format(Date(message.timestamp))
                        }
                    }
                }
            }

    // Calculate scaled font sizes based on user preference
    val scaledSmallSize = (12f * fontSize.scaleFactor).sp
    val scaledMediumSize = (14f * fontSize.scaleFactor).sp
    // Wider timestamp column for 12-hour format to fit "12:34 PM"
    val scaledTimeWidth =
            when (timestampFormat) {
                TimestampFormat.HOURS_12 -> (72f * fontSize.scaleFactor).dp
                else -> (48f * fontSize.scaleFactor).dp
            }

    val isMention =
            remember(message.content, currentNickname) {
                if (currentNickname.isNotEmpty() &&
                                !message.isSelf &&
                                message.type == MessageType.NORMAL
                ) {
                    message.content.contains(currentNickname, ignoreCase = true)
                } else {
                    false
                }
            }

    val backgroundModifier =
            if (isMention) {
                Modifier.fillMaxWidth().background(colors.green.copy(alpha = 0.15f))
            } else {
                Modifier.fillMaxWidth()
            }

    Column(modifier = backgroundModifier) {
        Row(modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
            Text(
                    text = formattedTime,
                    color = colors.timeColor,
                    fontSize = scaledSmallSize,
                    modifier = Modifier.width(scaledTimeWidth).alignByBaseline()
            )

            val nickColor =
                    when {
                        message.isSelf -> colors.msgSelf
                        message.type == MessageType.ACTION -> colors.msgAction
                        message.type in
                                listOf(MessageType.JOIN, MessageType.PART, MessageType.QUIT) ->
                                colors.comment
                        coloredNicknames ->
                                NickColorGenerator.getColorForNickname(
                                        message.sender,
                                        colors.isDark
                                )
                        else -> colors.foreground
                    }

            when (message.type) {
                MessageType.ACTION -> {
                    Text(
                            text = "* ${message.sender} ${message.content}",
                            color = colors.msgAction,
                            fontSize = scaledMediumSize,
                            modifier = Modifier.alignByBaseline()
                    )
                }
                MessageType.JOIN -> {
                    Text(
                            text = "-> ${message.sender} has joined",
                            color = colors.green,
                            fontSize = scaledSmallSize,
                            modifier = Modifier.alignByBaseline()
                    )
                }
                MessageType.PART, MessageType.QUIT -> {
                    Text(
                            text = "<- ${message.sender} has left: ${message.content}",
                            color = colors.red,
                            fontSize = scaledSmallSize,
                            modifier = Modifier.alignByBaseline()
                    )
                }
                MessageType.NICK -> {
                    Text(
                            text = "<> ${message.sender} ${message.content}",
                            color = colors.cyan,
                            fontSize = scaledSmallSize,
                            modifier = Modifier.alignByBaseline()
                    )
                }
                MessageType.SERVER, MessageType.SYSTEM -> {
                    Text(
                            text = message.content,
                            color = colors.comment,
                            fontSize = scaledSmallSize,
                            modifier = Modifier.alignByBaseline()
                    )
                }
                else -> {
                    ClickableMessageText(
                            text = "<${message.sender}> ${message.content}",
                            colors = colors,
                            textColor = colors.foreground,
                            nickname = message.sender,
                            nicknameColor = nickColor,
                            modifier = Modifier.fillMaxWidth().alignByBaseline(),
                            fontSize = scaledMediumSize
                    )
                }
            }
        }

        // Only show previews if not DISABLED
        if (urlImageDisplayMode != UrlImageDisplayMode.DISABLED &&
                        (imageUrls.isNotEmpty() || urlPreviews.isNotEmpty())
        ) {
            Column(
                    modifier =
                            Modifier.fillMaxWidth()
                                    .padding(start = 48.dp, top = 4.dp, bottom = 4.dp),
                    verticalArrangement = Arrangement.spacedBy(4.dp)
            ) {
                if (imageUrls.isNotEmpty()) {
                    when (urlImageDisplayMode) {
                        UrlImageDisplayMode.COMPACT -> CompactImageRow(imageUrls = imageUrls)
                        else -> ImageGallery(imageUrls = imageUrls)
                    }
                }

                urlPreviews.forEach { preview ->
                    when (urlImageDisplayMode) {
                        UrlImageDisplayMode.COMPACT -> CompactUrlPreviewCard(preview = preview)
                        else -> UrlPreviewCard(preview = preview)
                    }
                }
            }
        }
    }
}
