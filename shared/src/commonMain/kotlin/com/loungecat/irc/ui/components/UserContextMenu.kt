package com.loungecat.irc.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Message
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.loungecat.irc.data.model.ChannelUser
import com.loungecat.irc.ui.theme.AppColors

@Composable
fun UserContextMenu(
        user: ChannelUser,
        onDismiss: () -> Unit,
        onStartPM: (String) -> Unit,
        onWhois: ((String) -> Unit)? = null,
        onModerate: ((String) -> Unit)? = null,
        onToggleIgnore: ((String) -> Unit)? = null,
        isIgnored: Boolean = false
) {
    val colors = AppColors.current

    Dialog(onDismissRequest = onDismiss) {
        Surface(
                modifier = Modifier.width(280.dp),
                shape = RoundedCornerShape(16.dp),
                color = colors.windowBackground
        ) {
            Column(modifier = Modifier.fillMaxWidth()) {
                Row(
                        modifier =
                                Modifier.fillMaxWidth()
                                        .background(colors.currentLine)
                                        .padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically
                ) {
                    Icon(
                            imageVector =
                                    when {
                                        user.isOp -> Icons.Default.Shield
                                        user.isVoiced -> Icons.AutoMirrored.Filled.VolumeUp
                                        else -> Icons.Default.Person
                                    },
                            contentDescription = null,
                            tint =
                                    when {
                                        user.isOp -> colors.red
                                        user.isVoiced -> colors.green
                                        else -> colors.cyan
                                    },
                            modifier = Modifier.size(32.dp)
                    )

                    Spacer(modifier = Modifier.width(12.dp))

                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                                text = user.nickname,
                                style = MaterialTheme.typography.titleMedium,
                                fontWeight = FontWeight.Bold,
                                color = colors.foreground
                        )

                        val statusText =
                                when {
                                    user.isOp -> "Channel Operator"
                                    user.isVoiced -> "Voiced User"
                                    else -> "User"
                                }
                        Text(
                                text = statusText,
                                style = MaterialTheme.typography.bodySmall,
                                color = colors.comment
                        )
                    }

                    IconButton(onClick = onDismiss) {
                        Icon(
                                imageVector = Icons.Default.Close,
                                contentDescription = "Close",
                                tint = colors.foreground
                        )
                    }
                }

                HorizontalDivider(color = colors.border)

                Column(modifier = Modifier.fillMaxWidth()) {
                    UserActionItem(
                            icon = Icons.AutoMirrored.Filled.Message,
                            iconTint = colors.cyan,
                            title = "Send Private Message",
                            description = "Start a conversation",
                            onClick = {
                                onStartPM(user.nickname)
                                onDismiss()
                            }
                    )

                    if (onWhois != null) {
                        UserActionItem(
                                icon = Icons.Default.Info,
                                iconTint = colors.purple,
                                title = "User Info (WHOIS)",
                                description = "View detailed information",
                                onClick = {
                                    onWhois(user.nickname)
                                    onDismiss()
                                }
                        )
                    }

                    if (onModerate != null) {
                        UserActionItem(
                                icon = Icons.Default.Shield,
                                iconTint = colors.red,
                                title = "Moderation Actions",
                                description = "Kick, ban, or manage user",
                                onClick = {
                                    onModerate(user.nickname)
                                    onDismiss()
                                }
                        )
                    }

                    if (onToggleIgnore != null) {
                        UserActionItem(
                                icon =
                                        if (isIgnored) Icons.Default.Person
                                        else Icons.Default.PersonOff,
                                iconTint = if (isIgnored) colors.green else colors.orange,
                                title = if (isIgnored) "Unignore User" else "Ignore User",
                                description =
                                        if (isIgnored) "Show messages from user"
                                        else "Hide messages from user",
                                onClick = {
                                    onToggleIgnore(user.nickname)
                                    onDismiss()
                                }
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun UserActionItem(
        icon: ImageVector,
        iconTint: androidx.compose.ui.graphics.Color,
        title: String,
        description: String,
        onClick: () -> Unit
) {
    val colors = AppColors.current

    Row(
            modifier = Modifier.fillMaxWidth().clickable(onClick = onClick).padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(
                imageVector = icon,
                contentDescription = null,
                tint = iconTint,
                modifier = Modifier.size(24.dp)
        )

        Spacer(modifier = Modifier.width(16.dp))

        Column(modifier = Modifier.weight(1f)) {
            Text(
                    text = title,
                    style = MaterialTheme.typography.bodyMedium,
                    fontWeight = FontWeight.Medium,
                    color = colors.foreground
            )

            Text(
                    text = description,
                    style = MaterialTheme.typography.bodySmall,
                    color = colors.comment
            )
        }

        Icon(
                imageVector = Icons.Default.ChevronRight,
                contentDescription = null,
                tint = colors.comment,
                modifier = Modifier.size(20.dp)
        )
    }
}
