package com.loungecat.irc.data.model

enum class ModerationActionType {
    KICK,
    BAN,
    UNBAN,
    QUIET,
    UNQUIET,
    VOICE,
    DEVOICE,
    OP,
    DEOP,
    TOPIC,
    MODE,
    INVITE
}

data class ModerationAction(
    val type: ModerationActionType,
    val displayName: String,
    val description: String,
    val requiresTarget: Boolean = true,
    val requiresReason: Boolean = false,
    val requiresInput: Boolean = false,
    val inputLabel: String? = null,
    val requiresOp: Boolean = true
)

data class ChannelMode(
    val mode: Char,
    val displayName: String,
    val description: String,
    val hasParameter: Boolean = false,
    val isEnabled: Boolean = false
)

data class ModerationUser(
    val nickname: String,
    val username: String? = null,
    val hostname: String? = null,
    val isOp: Boolean = false,
    val isVoiced: Boolean = false,
    val isMe: Boolean = false
)

data class ChannelInfo(
    val name: String,
    val topic: String? = null,
    val modes: String? = null,
    val userCount: Int = 0,
    val isOp: Boolean = false
)

fun getAvailableModerationActions(): List<ModerationAction> = listOf(
    ModerationAction(ModerationActionType.KICK, "Kick User", "Remove user from channel", requiresReason = true),
    ModerationAction(ModerationActionType.BAN, "Ban User", "Ban user from channel", requiresReason = true),
    ModerationAction(ModerationActionType.UNBAN, "Unban User", "Remove ban from user"),
    ModerationAction(ModerationActionType.QUIET, "Quiet User", "Prevent user from speaking", requiresReason = true),
    ModerationAction(ModerationActionType.UNQUIET, "Unquiet User", "Allow user to speak again"),
    ModerationAction(ModerationActionType.VOICE, "Give Voice", "Grant voice status to user"),
    ModerationAction(ModerationActionType.DEVOICE, "Remove Voice", "Remove voice status from user"),
    ModerationAction(ModerationActionType.OP, "Give Op", "Grant operator status to user"),
    ModerationAction(ModerationActionType.DEOP, "Remove Op", "Remove operator status from user"),
    ModerationAction(ModerationActionType.TOPIC, "Change Topic", "Set channel topic", requiresTarget = false, requiresInput = true, inputLabel = "New Topic"),
    ModerationAction(ModerationActionType.MODE, "Set Mode", "Change channel modes", requiresTarget = false, requiresInput = true, inputLabel = "Mode String"),
    ModerationAction(ModerationActionType.INVITE, "Invite User", "Invite user to channel", requiresOp = false)
)

fun getCommonChannelModes(): List<ChannelMode> = listOf(
    ChannelMode('m', "Moderated", "Only voiced/ops can speak"),
    ChannelMode('i', "Invite Only", "Users must be invited"),
    ChannelMode('n', "No External", "No external messages"),
    ChannelMode('t', "Topic Lock", "Only ops can change topic"),
    ChannelMode('s', "Secret", "Channel is hidden"),
    ChannelMode('p', "Private", "Channel is private"),
    ChannelMode('k', "Key", "Channel requires password", hasParameter = true),
    ChannelMode('l', "Limit", "User limit", hasParameter = true)
)
