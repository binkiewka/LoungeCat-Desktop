package com.loungecat.irc.data.model

import java.util.UUID

data class IncomingMessage(
    val id: String = UUID.randomUUID().toString(),
    val target: String,
    val sender: String,
    val content: String,
    val type: MessageType = MessageType.NORMAL,
    val isSelf: Boolean = false,
    val timestamp: Long = System.currentTimeMillis()
)

enum class MessageType {
    NORMAL,
    ACTION,
    NOTICE,
    JOIN,
    PART,
    QUIT,
    KICK,
    NICK,
    TOPIC,
    MODE,
    ERROR,
    SERVER,
    SYSTEM,
    CTCP
}
