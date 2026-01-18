package com.loungecat.irc.ui.util

import com.loungecat.irc.data.model.IncomingMessage
import com.loungecat.irc.data.model.MessageType
import java.util.UUID

sealed interface ChatUiItem {
    val id: String
    val timestamp: Long

    data class SingleMessage(val message: IncomingMessage) : ChatUiItem {
        override val id: String = message.id
        override val timestamp: Long = message.timestamp
    }

    data class GroupedEvents(
            val events: List<IncomingMessage>,
            override val id: String = UUID.randomUUID().toString()
    ) : ChatUiItem {
        override val timestamp: Long = events.firstOrNull()?.timestamp ?: 0L
    }
}

fun groupMessages(messages: List<IncomingMessage>): List<ChatUiItem> {
    if (messages.isEmpty()) return emptyList()

    val result = mutableListOf<ChatUiItem>()
    val currentGroup = mutableListOf<IncomingMessage>()

    val groupableTypes = setOf(MessageType.JOIN, MessageType.PART, MessageType.QUIT)

    fun flushGroup() {
        if (currentGroup.isNotEmpty()) {
            result.add(ChatUiItem.GroupedEvents(currentGroup.toList()))
            currentGroup.clear()
        }
    }

    for (msg in messages) {
        if (msg.type in groupableTypes) {
            currentGroup.add(msg)
        } else {
            flushGroup()
            result.add(ChatUiItem.SingleMessage(msg))
        }
    }
    flushGroup()

    return result
}

fun List<ChatUiItem>.flattenToMessages(): List<IncomingMessage> {
    return flatMap { item ->
        when (item) {
            is ChatUiItem.SingleMessage -> listOf(item.message)
            is ChatUiItem.GroupedEvents -> item.events
        }
    }
}
