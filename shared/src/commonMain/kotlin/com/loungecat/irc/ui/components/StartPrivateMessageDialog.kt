package com.loungecat.irc.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.VolumeUp
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.loungecat.irc.data.model.ChannelUser
import com.loungecat.irc.ui.theme.AppColors

@Composable
fun StartPrivateMessageDialog(
    users: List<ChannelUser>,
    onDismiss: () -> Unit,
    onStartPM: (String) -> Unit
) {
    val colors = AppColors.current
    var searchQuery by remember { mutableStateOf("") }
    var manualNickname by remember { mutableStateOf("") }
    var showManualInput by remember { mutableStateOf(false) }
    
    val filteredUsers = remember(users, searchQuery) {
        if (searchQuery.isEmpty()) users
        else users.filter { it.nickname.contains(searchQuery, ignoreCase = true) }
    }
    
    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .fillMaxHeight(0.7f),
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
                    Text(
                        text = "Start Private Message",
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.Bold,
                        color = colors.foreground
                    )
                    
                    IconButton(onClick = onDismiss) {
                        Icon(
                            imageVector = Icons.Default.Close,
                            contentDescription = "Close",
                            tint = colors.foreground
                        )
                    }
                }
                
                OutlinedTextField(
                    value = searchQuery,
                    onValueChange = { searchQuery = it },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(16.dp),
                    placeholder = { Text("Search users...") },
                    leadingIcon = {
                        Icon(imageVector = Icons.Default.Search, contentDescription = "Search")
                    },
                    trailingIcon = {
                        if (searchQuery.isNotEmpty()) {
                            IconButton(onClick = { searchQuery = "" }) {
                                Icon(imageVector = Icons.Default.Clear, contentDescription = "Clear")
                            }
                        }
                    },
                    singleLine = true,
                    colors = OutlinedTextFieldDefaults.colors(
                        focusedTextColor = colors.foreground,
                        unfocusedTextColor = colors.foreground,
                        focusedBorderColor = colors.green,
                        unfocusedBorderColor = colors.border
                    )
                )
                
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .clickable { showManualInput = !showManualInput }
                        .padding(horizontal = 16.dp, vertical = 8.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Checkbox(checked = showManualInput, onCheckedChange = { showManualInput = it })
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(text = "Enter nickname manually", color = colors.foreground)
                }
                
                if (showManualInput) {
                    Row(
                        modifier = Modifier
                            .fillMaxWidth()
                            .padding(horizontal = 16.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        OutlinedTextField(
                            value = manualNickname,
                            onValueChange = { manualNickname = it },
                            modifier = Modifier.weight(1f),
                            placeholder = { Text("Enter nickname") },
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
                                if (manualNickname.isNotBlank()) {
                                    onStartPM(manualNickname.trim())
                                }
                            },
                            enabled = manualNickname.isNotBlank(),
                            colors = ButtonDefaults.buttonColors(containerColor = colors.green)
                        ) { Text("Start") }
                    }
                    
                    Spacer(modifier = Modifier.height(16.dp))
                }
                
                HorizontalDivider(color = colors.border)
                
                if (filteredUsers.isEmpty()) {
                    Box(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(32.dp),
                        contentAlignment = Alignment.Center
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Icon(
                                imageVector = Icons.Default.PersonOff,
                                contentDescription = null,
                                tint = colors.comment,
                                modifier = Modifier.size(64.dp)
                            )
                            Spacer(modifier = Modifier.height(16.dp))
                            Text(
                                text = if (searchQuery.isEmpty()) {
                                    "No users available\nJoin a channel first"
                                } else {
                                    "No users found"
                                },
                                color = colors.comment,
                                textAlign = TextAlign.Center
                            )
                        }
                    }
                } else {
                    LazyColumn(modifier = Modifier.fillMaxSize()) {
                        items(filteredUsers) { user ->
                            PMUserListItem(user = user, onClick = { onStartPM(user.nickname) })
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun PMUserListItem(user: ChannelUser, onClick: () -> Unit) {
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
                else -> colors.cyan
            },
            modifier = Modifier.size(32.dp)
        )
        
        Spacer(modifier = Modifier.width(16.dp))
        
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = user.nickname,
                style = MaterialTheme.typography.bodyLarge,
                fontWeight = FontWeight.Medium,
                color = colors.foreground
            )
            
            val statusText = when {
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
        
        Icon(
            imageVector = Icons.Default.ChevronRight,
            contentDescription = "Start PM",
            tint = colors.comment
        )
    }
    
    HorizontalDivider(color = colors.border)
}
