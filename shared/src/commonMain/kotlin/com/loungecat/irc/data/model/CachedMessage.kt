package com.loungecat.irc.data.model

import kotlinx.serialization.Serializable

@Serializable
data class CachedMessage(
    val id: String,
    val target: String,
    val sender: String,
    val content: String,
    val timestamp: Long,
    val type: String, // MessageType name
    val isSelf: Boolean
) {
    companion object {
        fun fromIncomingMessage(message: IncomingMessage): CachedMessage {
            return CachedMessage(
                id = message.id,
                target = message.target,
                sender = message.sender,
                content = message.content,
                timestamp = message.timestamp,
                type = message.type.name,
                isSelf = message.isSelf
            )
        }
    }

    fun toIncomingMessage(): IncomingMessage {
        return IncomingMessage(
            id = id,
            target = target,
            sender = sender,
            content = content,
            timestamp = timestamp,
            type = MessageType.valueOf(type),
            isSelf = isSelf
        )
    }
}

@Serializable
data class CachedChannel(
    val serverId: Long,
    val channelName: String,
    val messages: List<CachedMessage>
)
