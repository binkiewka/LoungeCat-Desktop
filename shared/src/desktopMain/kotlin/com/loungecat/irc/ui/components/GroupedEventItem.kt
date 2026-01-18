package com.loungecat.irc.ui.components

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.expandVertically
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.shrinkVertically
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.loungecat.irc.data.model.MessageType
import com.loungecat.irc.ui.theme.AppColors
import com.loungecat.irc.ui.util.ChatUiItem

@Composable
fun GroupedEventItem(item: ChatUiItem.GroupedEvents) {
    val colors = AppColors.current
    var isExpanded by remember { mutableStateOf(false) }

    val joinCount = item.events.count { it.type == MessageType.JOIN }
    val partCount = item.events.count { it.type == MessageType.PART }
    val quitCount = item.events.count { it.type == MessageType.QUIT }

    val parts = mutableListOf<String>()
    if (joinCount > 0) parts.add("$joinCount users have joined")
    if (partCount > 0) parts.add("$partCount user${if (partCount > 1) "s" else ""} has left")
    if (quitCount > 0) parts.add("$quitCount user${if (quitCount > 1) "s" else ""} has quit")

    val summaryText = parts.joinToString(", ")

    Column(modifier = Modifier.fillMaxWidth()) {
        Row(
                modifier =
                        Modifier.fillMaxWidth()
                                .clickable { isExpanded = !isExpanded }
                                .padding(vertical = 4.dp, horizontal = 8.dp),
                verticalAlignment = Alignment.CenterVertically
        ) {
            val icon =
                    if (isExpanded) Icons.Default.KeyboardArrowDown
                    else Icons.AutoMirrored.Filled.KeyboardArrowRight
            Icon(
                    imageVector = icon,
                    contentDescription = if (isExpanded) "Collapse" else "Expand",
                    tint = colors.comment,
                    modifier = Modifier.size(16.dp)
            )

            Text(
                    text = summaryText,
                    color = colors.comment,
                    fontSize = 12.sp,
                    modifier = Modifier.padding(start = 4.dp)
            )
        }

        AnimatedVisibility(
                visible = isExpanded,
                enter = expandVertically() + fadeIn(),
                exit = shrinkVertically() + fadeOut()
        ) {
            Column(modifier = Modifier.padding(start = 24.dp)) {
                item.events.forEach { message ->
                    val color =
                            when (message.type) {
                                MessageType.JOIN -> colors.green
                                MessageType.PART, MessageType.QUIT -> colors.red
                                else -> colors.comment
                            }
                    val action =
                            when (message.type) {
                                MessageType.JOIN -> "joined"
                                MessageType.PART -> "left"
                                MessageType.QUIT -> "quit"
                                else -> "unknown"
                            }
                    Text(
                            text = "${message.sender} has $action",
                            color = color,
                            fontSize = 12.sp,
                            modifier = Modifier.padding(vertical = 1.dp)
                    )
                }
            }
        }
    }
}
