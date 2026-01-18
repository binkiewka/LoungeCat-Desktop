package com.loungecat.irc.data.model

data class Channel(
        val name: String,
        val displayName: String? = null,
        val type: ChannelType,
        val users: List<ChannelUser> = emptyList(),
        val topic: String? = null,
        val unreadCount: Int = 0,
        val hasUnread: Boolean = false,
        val lastActivityTimestamp: Long = 0,
        val hasRecentActivity: Boolean = false
) {
        // Batch 4: Channel Statistics
        val stats: ChannelStats
                get() =
                        ChannelStats(
                                totalUsers = users.size,
                                opsCount = users.count { it.isOp },
                                voicedCount = users.count { it.isVoiced },
                                awayCount = users.count { it.isAway }
                        )
}

data class ChannelStats(
        val totalUsers: Int,
        val opsCount: Int,
        val voicedCount: Int,
        val awayCount: Int
) {
        val activeUsers: Int
                get() = totalUsers - awayCount
}

enum class ChannelType {
        CHANNEL,
        QUERY,
        SERVER
}

data class ChannelUser(
        val nickname: String,
        val modes: Set<UserMode> = emptySet(),
        // Batch 4: Away Status
        val isAway: Boolean = false,
        val awayMessage: String? = null
) {
        val isOp: Boolean
                get() =
                        modes.contains(UserMode.OP) ||
                                modes.contains(UserMode.OWNER) ||
                                modes.contains(UserMode.ADMIN)
        val isVoiced: Boolean
                get() = modes.contains(UserMode.VOICE)
}

enum class UserMode {
        OWNER,
        ADMIN,
        OP,
        HALFOP,
        VOICE
}
