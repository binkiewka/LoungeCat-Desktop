package com.loungecat.irc.ui.components

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.loungecat.irc.ui.theme.AppColors

@Composable
fun WhoisDialog(nickname: String, whoisData: String?, onDismiss: () -> Unit) {
    val colors = AppColors.current
    val scrollState = rememberScrollState()

    Dialog(onDismissRequest = onDismiss) {
        Card(
                colors =
                        CardDefaults.cardColors(
                                containerColor = colors.windowBackground,
                        ),
                elevation = CardDefaults.cardElevation(defaultElevation = 8.dp),
                modifier = Modifier.fillMaxWidth().padding(16.dp).heightIn(max = 500.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp).fillMaxWidth()) {
                Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                            text = "User Info: $nickname",
                            style = MaterialTheme.typography.titleLarge,
                            color = colors.foreground
                    )
                    IconButton(onClick = onDismiss) {
                        Icon(
                                imageVector = Icons.Default.Close,
                                contentDescription = "Close",
                                tint = colors.comment
                        )
                    }
                }

                HorizontalDivider(
                        modifier = Modifier.padding(vertical = 8.dp),
                        color = colors.border
                )

                Box(modifier = Modifier.weight(1f, fill = false).verticalScroll(scrollState)) {
                    if (whoisData.isNullOrBlank()) {
                        Box(
                                modifier = Modifier.fillMaxWidth().padding(32.dp),
                                contentAlignment = Alignment.Center
                        ) { CircularProgressIndicator(color = colors.cyan) }
                    } else {
                        SelectionContainer {
                            Text(
                                    text = whoisData,
                                    style = MaterialTheme.typography.bodyMedium,
                                    fontFamily = FontFamily.Monospace,
                                    color = colors.foreground,
                                    modifier = Modifier.fillMaxWidth()
                            )
                        }
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))

                Button(
                        onClick = onDismiss,
                        colors =
                                ButtonDefaults.buttonColors(
                                        containerColor = colors.buttonColor,
                                        contentColor = colors.windowBackground
                                ),
                        modifier = Modifier.align(Alignment.End)
                ) { Text("Close") }
            }
        }
    }
}
