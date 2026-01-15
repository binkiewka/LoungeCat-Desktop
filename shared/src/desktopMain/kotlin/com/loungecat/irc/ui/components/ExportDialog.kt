package com.loungecat.irc.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Dialog
import com.loungecat.irc.data.model.IncomingMessage
import com.loungecat.irc.util.ChatExporter
import java.io.File
import javax.swing.JFileChooser
import javax.swing.filechooser.FileNameExtensionFilter
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/** Dialog for exporting chat logs to various formats. */
@Composable
fun ExportDialog(channelName: String, messages: List<IncomingMessage>, onDismiss: () -> Unit) {
    val scope = rememberCoroutineScope()
    var selectedFormat by remember { mutableStateOf(ChatExporter.ExportFormat.TEXT) }
    var includeTimestamps by remember { mutableStateOf(true) }
    var isExporting by remember { mutableStateOf(false) }
    var exportResult by remember { mutableStateOf<String?>(null) }

    Dialog(onDismissRequest = onDismiss) {
        Surface(
                modifier = Modifier.width(400.dp).clip(RoundedCornerShape(12.dp)),
                color = MaterialTheme.colors.surface,
                elevation = 8.dp
        ) {
            Column(modifier = Modifier.padding(20.dp)) {
                Text(
                        text = "Export Chat Log",
                        style = MaterialTheme.typography.h6,
                        color = MaterialTheme.colors.primary
                )

                Spacer(modifier = Modifier.height(8.dp))

                Text(
                        text = "Channel: $channelName",
                        style = MaterialTheme.typography.body2,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f)
                )

                Text(
                        text = "Messages: ${messages.size}",
                        style = MaterialTheme.typography.body2,
                        color = MaterialTheme.colors.onSurface.copy(alpha = 0.7f)
                )

                Spacer(modifier = Modifier.height(16.dp))

                // Format selection
                Text(
                        text = "Export Format",
                        style = MaterialTheme.typography.subtitle2,
                        color = MaterialTheme.colors.onSurface
                )

                Spacer(modifier = Modifier.height(8.dp))

                Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    ChatExporter.ExportFormat.entries.forEach { format ->
                        FormatChip(
                                label = format.name,
                                selected = selectedFormat == format,
                                onClick = { selectedFormat = format }
                        )
                    }
                }

                Spacer(modifier = Modifier.height(16.dp))

                // Options
                Row(
                        verticalAlignment = Alignment.CenterVertically,
                        modifier = Modifier.clickable { includeTimestamps = !includeTimestamps }
                ) {
                    Checkbox(
                            checked = includeTimestamps,
                            onCheckedChange = { includeTimestamps = it },
                            colors =
                                    CheckboxDefaults.colors(
                                            checkedColor = MaterialTheme.colors.primary
                                    )
                    )
                    Text(
                            text = "Include timestamps",
                            style = MaterialTheme.typography.body2,
                            color = MaterialTheme.colors.onSurface
                    )
                }

                Spacer(modifier = Modifier.height(16.dp))

                // Result message
                exportResult?.let { result ->
                    Text(
                            text = result,
                            style = MaterialTheme.typography.body2,
                            color =
                                    if (result.startsWith("✓")) Color(0xFF50FA7B)
                                    else Color(0xFFFF5555)
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                }

                // Buttons
                Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.End,
                        verticalAlignment = Alignment.CenterVertically
                ) {
                    TextButton(onClick = onDismiss) { Text("Cancel") }

                    Spacer(modifier = Modifier.width(8.dp))

                    Button(
                            onClick = {
                                scope.launch {
                                    isExporting = true
                                    exportResult = null
                                    val result =
                                            exportChat(
                                                    messages = messages,
                                                    channelName = channelName,
                                                    format = selectedFormat,
                                                    includeTimestamps = includeTimestamps
                                            )
                                    exportResult = result
                                    isExporting = false
                                }
                            },
                            enabled = !isExporting && messages.isNotEmpty()
                    ) {
                        if (isExporting) {
                            CircularProgressIndicator(
                                    modifier = Modifier.size(16.dp),
                                    strokeWidth = 2.dp,
                                    color = MaterialTheme.colors.onPrimary
                            )
                        } else {
                            Text("Export")
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun FormatChip(label: String, selected: Boolean, onClick: () -> Unit) {
    val backgroundColor =
            if (selected) {
                MaterialTheme.colors.primary.copy(alpha = 0.2f)
            } else {
                Color.Transparent
            }
    val borderColor =
            if (selected) {
                MaterialTheme.colors.primary
            } else {
                MaterialTheme.colors.onSurface.copy(alpha = 0.3f)
            }

    Box(
            modifier =
                    Modifier.clip(RoundedCornerShape(16.dp))
                            .background(backgroundColor)
                            .border(1.dp, borderColor, RoundedCornerShape(16.dp))
                            .clickable(onClick = onClick)
                            .padding(horizontal = 16.dp, vertical = 8.dp)
    ) {
        Text(
                text = label,
                style = MaterialTheme.typography.body2,
                color =
                        if (selected) MaterialTheme.colors.primary
                        else MaterialTheme.colors.onSurface
        )
    }
}

private suspend fun exportChat(
        messages: List<IncomingMessage>,
        channelName: String,
        format: ChatExporter.ExportFormat,
        includeTimestamps: Boolean
): String =
        withContext(Dispatchers.IO) {
            try {
                // Generate the export content
                val content =
                        ChatExporter.export(
                                messages = messages,
                                channelName = channelName,
                                format = format,
                                includeTimestamp = includeTimestamps
                        )

                // Show file chooser
                val extension = ChatExporter.getFileExtension(format)
                val safeChannelName = channelName.replace(Regex("[^a-zA-Z0-9_-]"), "_")
                val defaultFileName = "${safeChannelName}_log.$extension"

                val fileChooser =
                        JFileChooser().apply {
                            dialogTitle = "Save Chat Log"
                            selectedFile = File(defaultFileName)
                            fileFilter =
                                    FileNameExtensionFilter(
                                            "${format.name} Files (*.$extension)",
                                            extension
                                    )
                        }

                val result = fileChooser.showSaveDialog(null)
                if (result == JFileChooser.APPROVE_OPTION) {
                    var file = fileChooser.selectedFile
                    // Ensure correct extension
                    if (!file.name.endsWith(".$extension")) {
                        file = File(file.absolutePath + ".$extension")
                    }

                    file.writeText(content)
                    "✓ Exported to: ${file.name}"
                } else {
                    "Export cancelled"
                }
            } catch (e: Exception) {
                "✗ Export failed: ${e.message}"
            }
        }
