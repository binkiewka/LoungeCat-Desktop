package com.loungecat.irc.util

import com.loungecat.irc.data.model.IncomingMessage
import com.loungecat.irc.data.model.MessageType
import java.text.SimpleDateFormat
import java.util.*
import kotlinx.serialization.Serializable
import kotlinx.serialization.encodeToString
import kotlinx.serialization.json.Json

/** Utility for exporting chat logs to various formats. */
object ChatExporter {

    enum class ExportFormat {
        TEXT,
        HTML,
        JSON
    }

    @Serializable
    data class ExportedMessage(
            val timestamp: Long,
            val formattedTime: String,
            val sender: String,
            val content: String,
            val type: String,
            val isSelf: Boolean
    )

    private val json = Json {
        prettyPrint = true
        encodeDefaults = true
    }

    /** Export messages to the specified format. */
    fun export(
            messages: List<IncomingMessage>,
            channelName: String,
            format: ExportFormat,
            includeTimestamp: Boolean = true
    ): String {
        return when (format) {
            ExportFormat.TEXT -> exportToText(messages, channelName, includeTimestamp)
            ExportFormat.HTML -> exportToHtml(messages, channelName, includeTimestamp)
            ExportFormat.JSON -> exportToJson(messages, channelName)
        }
    }

    /** Get the appropriate file extension for the format. */
    fun getFileExtension(format: ExportFormat): String {
        return when (format) {
            ExportFormat.TEXT -> "txt"
            ExportFormat.HTML -> "html"
            ExportFormat.JSON -> "json"
        }
    }

    private fun exportToText(
            messages: List<IncomingMessage>,
            channelName: String,
            includeTimestamp: Boolean
    ): String {
        val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault())
        val sb = StringBuilder()

        sb.appendLine("=== Chat Log: $channelName ===")
        sb.appendLine("Exported: ${dateFormat.format(Date())}")
        sb.appendLine("Messages: ${messages.size}")
        sb.appendLine("=".repeat(40))
        sb.appendLine()

        for (message in messages) {
            val time =
                    if (includeTimestamp) {
                        "[${dateFormat.format(Date(message.timestamp))}] "
                    } else ""

            val line =
                    when (message.type) {
                        MessageType.NORMAL -> "$time<${message.sender}> ${message.content}"
                        MessageType.ACTION -> "$time* ${message.sender} ${message.content}"
                        MessageType.JOIN -> "$time--> ${message.sender} has joined"
                        MessageType.PART ->
                                "$time<-- ${message.sender} has left${if (message.content.isNotBlank()) " (${message.content})" else ""}"
                        MessageType.QUIT ->
                                "$time<-- ${message.sender} has quit${if (message.content.isNotBlank()) " (${message.content})" else ""}"
                        MessageType.KICK -> "$time<-- ${message.content}"
                        MessageType.NICK -> "$time--- ${message.content}"
                        MessageType.TOPIC ->
                                "$time--- ${message.sender} set topic: ${message.content}"
                        MessageType.MODE ->
                                "$time--- ${message.sender} sets mode: ${message.content}"
                        MessageType.NOTICE -> "$time[${message.sender}] ${message.content}"
                        MessageType.SYSTEM -> "$time*** ${message.content}"
                        else -> "$time${message.content}"
                    }
            sb.appendLine(line)
        }

        return sb.toString()
    }

    private fun exportToHtml(
            messages: List<IncomingMessage>,
            channelName: String,
            includeTimestamp: Boolean
    ): String {
        val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault())
        val sb = StringBuilder()

        sb.appendLine(
                """
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>Chat Log: $channelName</title>
    <style>
        body { font-family: 'Consolas', 'Monaco', monospace; background: #282a36; color: #f8f8f2; padding: 20px; }
        .message { margin: 4px 0; }
        .timestamp { color: #6272a4; }
        .sender { color: #8be9fd; font-weight: bold; }
        .sender.self { color: #50fa7b; }
        .content { color: #f8f8f2; }
        .action { color: #ff79c6; font-style: italic; }
        .system { color: #6272a4; font-style: italic; }
        .join { color: #50fa7b; }
        .part, .quit { color: #ff5555; }
        h1 { color: #bd93f9; }
        .meta { color: #6272a4; margin-bottom: 20px; }
    </style>
</head>
<body>
    <h1>Chat Log: ${escapeHtml(channelName)}</h1>
    <p class="meta">Exported: ${dateFormat.format(Date())} | Messages: ${messages.size}</p>
    <div class="log">
""".trimIndent()
        )

        for (message in messages) {
            val time =
                    if (includeTimestamp) {
                        "<span class=\"timestamp\">[${dateFormat.format(Date(message.timestamp))}]</span> "
                    } else ""

            val selfClass = if (message.isSelf) " self" else ""

            val line =
                    when (message.type) {
                        MessageType.NORMAL ->
                                """
                    <div class="message">$time<span class="sender$selfClass">&lt;${escapeHtml(message.sender)}&gt;</span> <span class="content">${escapeHtml(message.content)}</span></div>
                """.trimIndent()
                        MessageType.ACTION ->
                                """
                    <div class="message action">$time* ${escapeHtml(message.sender)} ${escapeHtml(message.content)}</div>
                """.trimIndent()
                        MessageType.JOIN ->
                                """
                    <div class="message join">$time--&gt; ${escapeHtml(message.sender)} has joined</div>
                """.trimIndent()
                        MessageType.PART, MessageType.QUIT ->
                                """
                    <div class="message part">$time&lt;-- ${escapeHtml(message.sender)} has left</div>
                """.trimIndent()
                        MessageType.SYSTEM ->
                                """
                    <div class="message system">$time*** ${escapeHtml(message.content)}</div>
                """.trimIndent()
                        else ->
                                """
                    <div class="message">$time${escapeHtml(message.content)}</div>
                """.trimIndent()
                    }
            sb.appendLine(line)
        }

        sb.appendLine("""
    </div>
</body>
</html>
""".trimIndent())

        return sb.toString()
    }

    private fun exportToJson(messages: List<IncomingMessage>, channelName: String): String {
        val dateFormat = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault())

        val exportedMessages =
                messages.map { msg ->
                    ExportedMessage(
                            timestamp = msg.timestamp,
                            formattedTime = dateFormat.format(Date(msg.timestamp)),
                            sender = msg.sender,
                            content = msg.content,
                            type = msg.type.name,
                            isSelf = msg.isSelf
                    )
                }

        val exportData =
                mapOf(
                        "channel" to channelName,
                        "exportedAt" to dateFormat.format(Date()),
                        "messageCount" to messages.size,
                        "messages" to exportedMessages
                )

        return json.encodeToString(exportData)
    }

    private fun escapeHtml(text: String): String {
        return text.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;")
                .replace("\"", "&quot;")
                .replace("'", "&#39;")
    }
}
