package com.loungecat.irc.ui.screen

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.loungecat.irc.ui.theme.AppColors

@Composable
fun IgnoredUsersDialog(
        ignoredUsers: Set<String>,
        onDismiss: () -> Unit,
        onUnignore: (String) -> Unit
) {
    val colors = AppColors.current

    Dialog(onDismissRequest = onDismiss) {
        Surface(
                shape = MaterialTheme.shapes.medium,
                color = colors.windowBackground,
                modifier = Modifier.width(400.dp).heightIn(max = 500.dp)
        ) {
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                        text = "Ignored Users",
                        style = MaterialTheme.typography.titleMedium,
                        color = colors.cyan,
                        modifier = Modifier.padding(bottom = 16.dp)
                )

                if (ignoredUsers.isEmpty()) {
                    Box(
                            modifier = Modifier.fillMaxWidth().height(100.dp),
                            contentAlignment = Alignment.Center
                    ) {
                        Text(
                                text = "No ignored users",
                                style = MaterialTheme.typography.bodyMedium,
                                color = colors.comment
                        )
                    }
                } else {
                    LazyColumn(
                            modifier = Modifier.weight(1f, fill = false),
                            verticalArrangement = Arrangement.spacedBy(8.dp)
                    ) {
                        items(ignoredUsers.toList().sorted()) { user ->
                            Row(
                                    modifier =
                                            Modifier.fillMaxWidth()
                                                    .background(
                                                            colors.background,
                                                            shape = RoundedCornerShape(4.dp)
                                                    )
                                                    .padding(8.dp),
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.SpaceBetween
                            ) {
                                Text(
                                        text = user,
                                        style = MaterialTheme.typography.bodyMedium,
                                        color = colors.foreground
                                )
                                IconButton(onClick = { onUnignore(user) }) {
                                    Icon(
                                            imageVector = Icons.Default.Close,
                                            contentDescription = "Unignore",
                                            tint = colors.red
                                    )
                                }
                            }
                        }
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                Button(
                        onClick = onDismiss,
                        modifier = Modifier.align(Alignment.End),
                        colors =
                                ButtonDefaults.buttonColors(
                                        containerColor = colors.background,
                                        contentColor = colors.foreground
                                )
                ) { Text("Close") }
            }
        }
    }
}
