package com.loungecat.irc.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ExitToApp
import androidx.compose.material.icons.automirrored.filled.VolumeMute
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.loungecat.irc.ui.theme.AppColors

@Composable
fun QuickModerationDialog(
    targetNickname: String,
    channelName: String,
    onDismiss: () -> Unit,
    onAction: (String) -> Unit
) {
    val colors = AppColors.current
    var kickReason by remember { mutableStateOf("") }
    var showKickConfirm by remember { mutableStateOf(false) }
    
    if (showKickConfirm) {
        AlertDialog(
            onDismissRequest = { showKickConfirm = false },
            title = { Text("Kick $targetNickname", color = colors.foreground) },
            text = {
                Column {
                    Text("Reason (optional):", color = colors.comment)
                    Spacer(modifier = Modifier.height(8.dp))
                    OutlinedTextField(
                        value = kickReason,
                        onValueChange = { kickReason = it },
                        placeholder = { Text("Enter reason") },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true
                    )
                }
            },
            confirmButton = {
                Button(
                    onClick = {
                        val reason = kickReason.ifEmpty { "Kicked" }
                        onAction("/kick $targetNickname $reason")
                    },
                    colors = ButtonDefaults.buttonColors(containerColor = colors.red)
                ) { Text("Kick") }
            },
            dismissButton = {
                TextButton(onClick = { showKickConfirm = false }) {
                    Text("Cancel", color = colors.comment)
                }
            },
            containerColor = colors.windowBackground
        )
        return
    }
    
    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier.width(320.dp),
            shape = RoundedCornerShape(16.dp),
            color = colors.windowBackground
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Moderate: $targetNickname",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = colors.foreground
                )
                Text(
                    text = "in $channelName",
                    style = MaterialTheme.typography.bodySmall,
                    color = colors.comment
                )
                
                Spacer(modifier = Modifier.height(16.dp))
                
                QuickActionButton(
                    icon = Icons.AutoMirrored.Filled.ExitToApp,
                    text = "Kick",
                    color = colors.red,
                    onClick = { showKickConfirm = true }
                )
                
                Spacer(modifier = Modifier.height(8.dp))
                
                QuickActionButton(
                    icon = Icons.Default.Block,
                    text = "Ban",
                    color = colors.red,
                    onClick = { onAction("/ban $targetNickname") }
                )
                
                Spacer(modifier = Modifier.height(8.dp))
                
                QuickActionButton(
                    icon = Icons.AutoMirrored.Filled.VolumeUp,
                    text = "Voice (+v)",
                    color = colors.green,
                    onClick = { onAction("/voice $targetNickname") }
                )
                
                Spacer(modifier = Modifier.height(8.dp))
                
                QuickActionButton(
                    icon = Icons.AutoMirrored.Filled.VolumeMute,
                    text = "DeVoice (-v)",
                    color = colors.orange,
                    onClick = { onAction("/devoice $targetNickname") }
                )
                
                Spacer(modifier = Modifier.height(8.dp))
                
                QuickActionButton(
                    icon = Icons.Default.Shield,
                    text = "Op (+o)",
                    color = colors.purple,
                    onClick = { onAction("/op $targetNickname") }
                )
                
                Spacer(modifier = Modifier.height(8.dp))
                
                QuickActionButton(
                    icon = Icons.Default.RemoveCircle,
                    text = "DeOp (-o)",
                    color = colors.orange,
                    onClick = { onAction("/deop $targetNickname") }
                )
                
                Spacer(modifier = Modifier.height(16.dp))
                
                TextButton(
                    onClick = onDismiss,
                    modifier = Modifier.align(Alignment.End)
                ) {
                    Text("Cancel", color = colors.comment)
                }
            }
        }
    }
}

@Composable
private fun QuickActionButton(
    icon: ImageVector,
    text: String,
    color: androidx.compose.ui.graphics.Color,
    onClick: () -> Unit
) {
    val colors = AppColors.current
    
    Button(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        colors = ButtonDefaults.buttonColors(containerColor = color)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.Start,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(imageVector = icon, contentDescription = null)
            Spacer(modifier = Modifier.width(12.dp))
            Text(text = text)
        }
    }
}
