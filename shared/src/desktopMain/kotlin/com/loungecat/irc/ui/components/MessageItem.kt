package com.loungecat.irc.ui.components

import androidx.compose.foundation.ContextMenuArea
import androidx.compose.foundation.ContextMenuItem
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.loungecat.irc.data.model.*
import com.loungecat.irc.ui.selection.SelectionController
import com.loungecat.irc.ui.theme.AppColors
import com.loungecat.irc.util.NickColorGenerator
import java.text.SimpleDateFormat
import java.util.*

@OptIn(androidx.compose.foundation.ExperimentalFoundationApi::class)
@Composable
fun MessageItem(
        message: IncomingMessage,
        urlPreviews: List<UrlPreview> = emptyList(),
        imageUrls: List<String> = emptyList(),
        currentNickname: String = "",
        urlImageDisplayMode: UrlImageDisplayMode = UrlImageDisplayMode.INLINE,
        fontSize: FontSize = FontSize.MEDIUM,
        timestampFormat: TimestampFormat = TimestampFormat.HOURS_24,
        coloredNicknames: Boolean = true,
        whoisInfo: Map<String, String> = emptyMap(),
        onRequestWhois: ((String) -> Unit)? = null,
        selectionController: SelectionController? = null,
        selectionHighlightColor: Color = Color.Blue.copy(alpha = 0.3f),
        onCopySelection: ((String) -> Unit)? = null
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
                                                        SimpleDateFormat(
                                                                        "MM/dd",
                                                                        Locale.getDefault()
                                                                )
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

        val clipboardManager = LocalClipboardManager.current

        // Unregister from selection controller when leaving composition
        DisposableEffect(message.id) {
                onDispose { selectionController?.unregisterTextLayout(message.id) }
        }

        // Selection highlight modifier
        val selectionModifier =
                if (selectionController != null) {
                        Modifier.drawBehind {
                                val rects =
                                        selectionController.getSelectionRectsForMessage(message.id)
                                rects.forEach { rect ->
                                        drawRect(
                                                color = selectionHighlightColor,
                                                topLeft = rect.topLeft,
                                                size = rect.size
                                        )
                                }
                        }
                } else {
                        Modifier
                }

        Column(modifier = backgroundModifier.then(selectionModifier)) {
                // [MODIFIED] ContextMenuArea wraps the whole row.
                // Now includes Copy Selection when text is selected.
                ContextMenuArea(
                        items = {
                                buildList {
                                        // Add Copy Selection if we have a selection
                                        if (selectionController?.state?.hasSelection() == true) {
                                                add(
                                                        ContextMenuItem("Copy Selection") {
                                                                val selectedText =
                                                                        selectionController
                                                                                .getSelectedText()
                                                                if (selectedText.isNotEmpty()) {
                                                                        onCopySelection?.invoke(
                                                                                selectedText
                                                                        )
                                                                                ?: clipboardManager
                                                                                        .setText(
                                                                                                AnnotatedString(
                                                                                                        selectedText
                                                                                                )
                                                                                        )
                                                                }
                                                        }
                                                )
                                        }
                                        add(
                                                ContextMenuItem("Copy Message") {
                                                        clipboardManager.setText(
                                                                AnnotatedString(message.content)
                                                        )
                                                }
                                        )
                                        add(
                                                ContextMenuItem("Copy Sender") {
                                                        clipboardManager.setText(
                                                                AnnotatedString(message.sender)
                                                        )
                                                }
                                        )
                                }
                        }
                ) {
                        Row(modifier = Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
                                // Timestamp
                                Text(
                                        text = formattedTime,
                                        color = colors.timeColor,
                                        fontSize = scaledSmallSize,
                                        modifier = Modifier.width(scaledTimeWidth).alignByBaseline()
                                )

                                val nickColor =
                                        when {
                                                message.isSelf -> colors.msgSelf
                                                message.type == MessageType.ACTION ->
                                                        colors.msgAction
                                                message.type in
                                                        listOf(
                                                                MessageType.JOIN,
                                                                MessageType.PART,
                                                                MessageType.QUIT
                                                        ) -> colors.comment
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
                                                        text =
                                                                "* ${message.sender} ${message.content}",
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
                                                        text =
                                                                "<- ${message.sender} has left: ${message.content}",
                                                        color = colors.red,
                                                        fontSize = scaledSmallSize,
                                                        modifier = Modifier.alignByBaseline()
                                                )
                                        }
                                        MessageType.NICK -> {
                                                Text(
                                                        text =
                                                                "<> ${message.sender} ${message.content}",
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
                                                // 1. Nickname with Tooltip
                                                val nickText = "${message.sender}: "
                                                val whois = whoisInfo[message.sender.lowercase()]

                                                androidx.compose.foundation.TooltipArea(
                                                        tooltip = {
                                                                if (whois != null) {
                                                                        Text(
                                                                                text = whois,
                                                                                color =
                                                                                        colors.foreground,
                                                                                fontSize = 12.sp,
                                                                                modifier =
                                                                                        Modifier.shadow(
                                                                                                        4.dp,
                                                                                                        RoundedCornerShape(
                                                                                                                4.dp
                                                                                                        )
                                                                                                )
                                                                                                .background(
                                                                                                        colors.windowBackground
                                                                                                                .copy(
                                                                                                                        alpha =
                                                                                                                                0.95f
                                                                                                                ),
                                                                                                        RoundedCornerShape(
                                                                                                                4.dp
                                                                                                        )
                                                                                                )
                                                                                                .padding(
                                                                                                        8.dp
                                                                                                )
                                                                        )
                                                                } else {
                                                                        // Trigger loading state
                                                                        Text(
                                                                                text =
                                                                                        "Loading WHOIS for ${message.sender}...",
                                                                                color =
                                                                                        colors.foreground,
                                                                                fontSize = 12.sp,
                                                                                modifier =
                                                                                        Modifier.shadow(
                                                                                                        4.dp,
                                                                                                        RoundedCornerShape(
                                                                                                                4.dp
                                                                                                        )
                                                                                                )
                                                                                                .background(
                                                                                                        colors.windowBackground
                                                                                                                .copy(
                                                                                                                        alpha =
                                                                                                                                0.95f
                                                                                                                ),
                                                                                                        RoundedCornerShape(
                                                                                                                4.dp
                                                                                                        )
                                                                                                )
                                                                                                .padding(
                                                                                                        8.dp
                                                                                                )
                                                                        )
                                                                        // Side effect to request if
                                                                        // not present
                                                                        if (onRequestWhois != null
                                                                        ) {
                                                                                androidx.compose
                                                                                        .runtime
                                                                                        .LaunchedEffect(
                                                                                                message.sender
                                                                                        ) {
                                                                                                onRequestWhois(
                                                                                                        message.sender
                                                                                                )
                                                                                        }
                                                                        }
                                                                }
                                                        },
                                                        delayMillis = 600
                                                ) {
                                                        Text(
                                                                text = nickText,
                                                                color = nickColor,
                                                                fontSize = scaledMediumSize,
                                                                modifier =
                                                                        Modifier.alignByBaseline()
                                                        )
                                                }

                                                // 2. Message Content
                                                // Reduced to bare component to ensure Baseline
                                                // Alignment works (no internal layouts)
                                                ClickableMessageText(
                                                        text = message.content,
                                                        colors = colors,
                                                        textColor = colors.foreground,
                                                        nickname = message.sender,
                                                        nicknameColor = nickColor,
                                                        modifier =
                                                                Modifier.fillMaxWidth()
                                                                        .alignByBaseline(),
                                                        fontSize = scaledMediumSize,
                                                        whoisInfo =
                                                                null, // Disable internal tooltip
                                                        // hooks
                                                        onRequestWhois =
                                                                null // Disable internal tooltip
                                                        // hooks
                                                        )
                                        }
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
                                                UrlImageDisplayMode.COMPACT ->
                                                        CompactImageRow(imageUrls = imageUrls)
                                                else -> ImageGallery(imageUrls = imageUrls)
                                        }
                                }

                                urlPreviews.forEach { preview ->
                                        when (urlImageDisplayMode) {
                                                UrlImageDisplayMode.COMPACT ->
                                                        CompactUrlPreviewCard(preview = preview)
                                                else -> UrlPreviewCard(preview = preview)
                                        }
                                }
                        }
                }
        }
}
