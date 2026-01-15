package com.loungecat.irc.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.*
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.loungecat.irc.data.model.*
import com.loungecat.irc.ui.theme.AppColors

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ModerationDialog(
    channelInfo: ChannelInfo,
    users: List<ModerationUser>,
    onDismiss: () -> Unit,
    onAction: (ModerationActionType, String?, String?, String?) -> Unit
) {
    val colors = AppColors.current
    var selectedTab by remember { mutableStateOf(0) }
    
    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .fillMaxHeight(0.85f),
            shape = RoundedCornerShape(16.dp),
            color = colors.windowBackground
        ) {
            Column(modifier = Modifier.fillMaxSize()) {
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(colors.currentLine)
                        .padding(16.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text(
                            text = "Channel Moderation",
                            style = MaterialTheme.typography.titleLarge,
                            fontWeight = FontWeight.Bold,
                            color = colors.foreground
                        )
                        Text(
                            text = channelInfo.name,
                            style = MaterialTheme.typography.bodyMedium,
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
                
                PrimaryTabRow(
                    selectedTabIndex = selectedTab,
                    containerColor = colors.windowBackground,
                    contentColor = colors.foreground
                ) {
                    Tab(selected = selectedTab == 0, onClick = { selectedTab = 0 }, text = { Text("Users") })
                    Tab(selected = selectedTab == 1, onClick = { selectedTab = 1 }, text = { Text("Topic") })
                    Tab(selected = selectedTab == 2, onClick = { selectedTab = 2 }, text = { Text("Modes") })
                }
                
                when (selectedTab) {
                    0 -> UserModerationTab(users, channelInfo, onAction)
                    1 -> TopicTab(channelInfo, onAction)
                    2 -> ModesTab(channelInfo, onAction)
                }
            }
        }
    }
}

@Composable
private fun UserModerationTab(
    users: List<ModerationUser>,
    channelInfo: ChannelInfo,
    onAction: (ModerationActionType, String?, String?, String?) -> Unit
) {
    val colors = AppColors.current
    var selectedUser by remember { mutableStateOf<ModerationUser?>(null) }
    
    if (selectedUser != null) {
        UserActionDialog(
            user = selectedUser!!,
            channelInfo = channelInfo,
            onDismiss = { selectedUser = null },
            onAction = { action, reason ->
                onAction(action, selectedUser!!.nickname, reason, null)
                selectedUser = null
            }
        )
    }
    
    LazyColumn(modifier = Modifier.fillMaxSize()) {
        items(users) { user ->
            ModerationUserItem(user = user, onClick = { selectedUser = user })
        }
    }
}

@Composable
private fun ModerationUserItem(user: ModerationUser, onClick: () -> Unit) {
    val colors = AppColors.current
    
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable(onClick = onClick)
            .padding(16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Icon(
            imageVector = when {
                user.isOp -> Icons.Default.Shield
                user.isVoiced -> Icons.AutoMirrored.Filled.VolumeUp
                else -> Icons.Default.Person
            },
            contentDescription = null,
            tint = when {
                user.isOp -> colors.red
                user.isVoiced -> colors.green
                else -> colors.comment
            },
            modifier = Modifier.size(24.dp)
        )
        
        Spacer(modifier = Modifier.width(12.dp))
        
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = user.nickname,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = if (user.isMe) FontWeight.Bold else FontWeight.Normal,
                color = if (user.isMe) colors.cyan else colors.foreground
            )
            
            if (user.username != null && user.hostname != null) {
                Text(
                    text = "${user.username}@${user.hostname}",
                    style = MaterialTheme.typography.bodySmall,
                    color = colors.comment
                )
            }
        }
        
        Icon(
            imageVector = Icons.Default.ChevronRight,
            contentDescription = "Actions",
            tint = colors.comment
        )
    }
    
    HorizontalDivider(color = colors.border)
}

@Composable
private fun UserActionDialog(
    user: ModerationUser,
    channelInfo: ChannelInfo,
    onDismiss: () -> Unit,
    onAction: (ModerationActionType, String?) -> Unit
) {
    val colors = AppColors.current
    val actions = getAvailableModerationActions().filter { 
        it.requiresTarget && (!it.requiresOp || channelInfo.isOp)
    }
    
    var selectedAction by remember { mutableStateOf<ModerationAction?>(null) }
    
    if (selectedAction != null) {
        ActionConfirmDialog(
            action = selectedAction!!,
            targetUser = user.nickname,
            onDismiss = { selectedAction = null },
            onConfirm = { reason ->
                onAction(selectedAction!!.type, reason)
                selectedAction = null
            }
        )
    }
    
    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(16.dp),
            color = colors.windowBackground
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Actions for ${user.nickname}",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = colors.foreground
                )
                
                Spacer(modifier = Modifier.height(16.dp))
                
                actions.forEach { action ->
                    ModerationActionButton(action = action, onClick = { selectedAction = action })
                    Spacer(modifier = Modifier.height(8.dp))
                }
                
                Spacer(modifier = Modifier.height(8.dp))
                
                Button(
                    onClick = onDismiss,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.buttonColors(containerColor = colors.comment)
                ) {
                    Text("Cancel")
                }
            }
        }
    }
}

@Composable
private fun ModerationActionButton(action: ModerationAction, onClick: () -> Unit) {
    val colors = AppColors.current
    
    Button(
        onClick = onClick,
        modifier = Modifier.fillMaxWidth(),
        colors = ButtonDefaults.buttonColors(
            containerColor = when (action.type) {
                ModerationActionType.KICK, ModerationActionType.BAN -> colors.red
                ModerationActionType.OP, ModerationActionType.VOICE -> colors.green
                else -> colors.purple
            }
        )
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(text = action.displayName, fontWeight = FontWeight.Bold)
                Text(text = action.description, style = MaterialTheme.typography.bodySmall)
            }
            
            Icon(imageVector = getActionIcon(action.type), contentDescription = null)
        }
    }
}

@Composable
private fun ActionConfirmDialog(
    action: ModerationAction,
    targetUser: String,
    onDismiss: () -> Unit,
    onConfirm: (String?) -> Unit
) {
    val colors = AppColors.current
    var reason by remember { mutableStateOf("") }
    
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(text = action.displayName, color = colors.foreground) },
        text = {
            Column {
                Text(text = "Target: $targetUser", color = colors.comment)
                
                if (action.requiresReason) {
                    Spacer(modifier = Modifier.height(16.dp))
                    
                    OutlinedTextField(
                        value = reason,
                        onValueChange = { reason = it },
                        label = { Text("Reason (optional)") },
                        modifier = Modifier.fillMaxWidth(),
                        colors = OutlinedTextFieldDefaults.colors(
                            focusedTextColor = colors.foreground,
                            unfocusedTextColor = colors.foreground,
                            focusedBorderColor = colors.green,
                            unfocusedBorderColor = colors.border
                        )
                    )
                }
            }
        },
        confirmButton = {
            Button(
                onClick = { onConfirm(reason.ifEmpty { null }) },
                colors = ButtonDefaults.buttonColors(containerColor = colors.green)
            ) { Text("Confirm") }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Cancel", color = colors.comment) }
        },
        containerColor = colors.windowBackground
    )
}

@Composable
private fun TopicTab(
    channelInfo: ChannelInfo,
    onAction: (ModerationActionType, String?, String?, String?) -> Unit
) {
    val colors = AppColors.current
    var newTopic by remember { mutableStateOf(channelInfo.topic ?: "") }
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        Text(
            text = "Current Topic",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = colors.foreground
        )
        
        Spacer(modifier = Modifier.height(8.dp))
        
        Text(
            text = channelInfo.topic ?: "No topic set",
            style = MaterialTheme.typography.bodyMedium,
            color = if (channelInfo.topic != null) colors.foreground else colors.comment
        )
        
        Spacer(modifier = Modifier.height(24.dp))
        
        Text(
            text = "New Topic",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = colors.foreground
        )
        
        Spacer(modifier = Modifier.height(8.dp))
        
        OutlinedTextField(
            value = newTopic,
            onValueChange = { newTopic = it },
            modifier = Modifier.fillMaxWidth(),
            placeholder = { Text("Enter new topic") },
            minLines = 3,
            maxLines = 5,
            colors = OutlinedTextFieldDefaults.colors(
                focusedTextColor = colors.foreground,
                unfocusedTextColor = colors.foreground,
                focusedBorderColor = colors.green,
                unfocusedBorderColor = colors.border
            )
        )
        
        Spacer(modifier = Modifier.height(16.dp))
        
        Button(
            onClick = { onAction(ModerationActionType.TOPIC, null, null, newTopic) },
            modifier = Modifier.fillMaxWidth(),
            enabled = channelInfo.isOp && newTopic.isNotEmpty(),
            colors = ButtonDefaults.buttonColors(containerColor = colors.green)
        ) {
            Icon(imageVector = Icons.Default.Edit, contentDescription = null)
            Spacer(modifier = Modifier.width(8.dp))
            Text("Set Topic")
        }
        
        if (!channelInfo.isOp) {
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = "You need operator status to change the topic",
                style = MaterialTheme.typography.bodySmall,
                color = colors.red
            )
        }
    }
}

@Composable
private fun ModesTab(
    channelInfo: ChannelInfo,
    onAction: (ModerationActionType, String?, String?, String?) -> Unit
) {
    val colors = AppColors.current
    val commonModes = getCommonChannelModes()
    var customMode by remember { mutableStateOf("") }
    
    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(16.dp)
    ) {
        Text(
            text = "Current Modes",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = colors.foreground
        )
        
        Spacer(modifier = Modifier.height(8.dp))
        
        Text(
            text = channelInfo.modes ?: "No modes set",
            style = MaterialTheme.typography.bodyMedium,
            color = if (channelInfo.modes != null) colors.foreground else colors.comment
        )
        
        Spacer(modifier = Modifier.height(24.dp))
        
        Text(
            text = "Common Modes",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = colors.foreground
        )
        
        Spacer(modifier = Modifier.height(8.dp))
        
        LazyColumn(modifier = Modifier.weight(1f)) {
            items(commonModes) { mode ->
                ModeItem(
                    mode = mode,
                    onToggle = { enabled ->
                        val modeString = if (enabled) "+${mode.mode}" else "-${mode.mode}"
                        onAction(ModerationActionType.MODE, null, null, modeString)
                    },
                    enabled = channelInfo.isOp
                )
            }
        }
        
        Spacer(modifier = Modifier.height(16.dp))
        
        Text(
            text = "Custom Mode",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = colors.foreground
        )
        
        Spacer(modifier = Modifier.height(8.dp))
        
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            OutlinedTextField(
                value = customMode,
                onValueChange = { customMode = it },
                modifier = Modifier.weight(1f),
                placeholder = { Text("+m, -i, +l 50") },
                singleLine = true,
                colors = OutlinedTextFieldDefaults.colors(
                    focusedTextColor = colors.foreground,
                    unfocusedTextColor = colors.foreground,
                    focusedBorderColor = colors.green,
                    unfocusedBorderColor = colors.border
                )
            )
            
            Spacer(modifier = Modifier.width(8.dp))
            
            Button(
                onClick = {
                    onAction(ModerationActionType.MODE, null, null, customMode)
                    customMode = ""
                },
                enabled = channelInfo.isOp && customMode.isNotEmpty()
            ) { Text("Set") }
        }
    }
}

@Composable
private fun ModeItem(mode: ChannelMode, onToggle: (Boolean) -> Unit, enabled: Boolean) {
    val colors = AppColors.current
    
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 8.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = "${mode.displayName} (+${mode.mode})",
                style = MaterialTheme.typography.bodyLarge,
                color = colors.foreground
            )
            Text(
                text = mode.description,
                style = MaterialTheme.typography.bodySmall,
                color = colors.comment
            )
        }
        
        Switch(checked = mode.isEnabled, onCheckedChange = onToggle, enabled = enabled)
    }
}

private fun getActionIcon(type: ModerationActionType): ImageVector = when (type) {
    ModerationActionType.KICK -> Icons.AutoMirrored.Filled.ExitToApp
    ModerationActionType.BAN -> Icons.Default.Block
    ModerationActionType.UNBAN -> Icons.Default.CheckCircle
    ModerationActionType.QUIET -> Icons.AutoMirrored.Filled.VolumeOff
    ModerationActionType.UNQUIET -> Icons.AutoMirrored.Filled.VolumeUp
    ModerationActionType.VOICE -> Icons.AutoMirrored.Filled.VolumeUp
    ModerationActionType.DEVOICE -> Icons.AutoMirrored.Filled.VolumeMute
    ModerationActionType.OP -> Icons.Default.Shield
    ModerationActionType.DEOP -> Icons.Default.RemoveCircle
    ModerationActionType.TOPIC -> Icons.Default.Edit
    ModerationActionType.MODE -> Icons.Default.Settings
    ModerationActionType.INVITE -> Icons.Default.PersonAdd
}
