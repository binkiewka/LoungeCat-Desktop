package com.loungecat.irc.ui.components

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.People
import androidx.compose.material.icons.filled.PeopleOutline
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.loungecat.irc.data.model.ChannelUser
import com.loungecat.irc.data.model.UserMode
import com.loungecat.irc.data.model.UserPreferences
import com.loungecat.irc.service.DesktopConnectionManager
import com.loungecat.irc.ui.theme.AppColors

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ChannelPane(
        serverId: Long,
        channelName: String,
        isActive: Boolean = false, // This flag determines if the pane is focused
        markAsRead: Boolean = isActive, // Determines if messages should be marked as read
        onPaneActive: () -> Unit = {},
        connectionManager: DesktopConnectionManager,
        userPreferences: UserPreferences,
        onSendMessage: (String) -> Unit,
        onJoinChannel: () -> Unit = {},
        onClosePane: (() -> Unit)? = null,
        onUserClick: (ChannelUser) -> Unit = {}
) {
        val colors = AppColors.current
        var isUserListVisible by remember { mutableStateOf(true) }

        val connectionStates by connectionManager.connectionStates.collectAsState()
        val connection = connectionStates[serverId]
        // Accessing users via connection directly to ensure we get users for the specific server
        val channel = connection?.channels?.find { it.name.equals(channelName, ignoreCase = true) }
        val channelUsers = channel?.users ?: emptyList()
        val serverName = connection?.config?.serverName ?: connection?.config?.hostname ?: ""
        val topic = channel?.topic

        Column(modifier = Modifier.fillMaxSize()) {
                // Header
                Row(
                        modifier =
                                Modifier.fillMaxWidth()
                                        .background(colors.windowBackground)
                                        .padding(8.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                ) {
                        Column(modifier = Modifier.weight(1f)) {
                                Text(
                                        text = channel?.displayName ?: channelName,
                                        style = MaterialTheme.typography.titleMedium,
                                        color = colors.foreground,
                                        fontWeight = FontWeight.Bold
                                )
                                Row(verticalAlignment = Alignment.CenterVertically) {
                                        Text(
                                                text = serverName,
                                                style = MaterialTheme.typography.bodySmall,
                                                color = colors.comment
                                        )
                                        if (!topic.isNullOrBlank()) {
                                                Text(
                                                        text = " | ",
                                                        style = MaterialTheme.typography.bodySmall,
                                                        color = colors.comment
                                                )
                                                AppTooltip(topic) {
                                                        Text(
                                                                text = topic,
                                                                style =
                                                                        MaterialTheme.typography
                                                                                .bodySmall,
                                                                color = colors.foreground,
                                                                maxLines = 1,
                                                                overflow =
                                                                        androidx.compose.ui.text
                                                                                .style.TextOverflow
                                                                                .Ellipsis
                                                        )
                                                }
                                        }
                                }
                        }

                        Row {
                                if (channelName.isNotEmpty()) {
                                        AppTooltip(
                                                text =
                                                        if (isUserListVisible) "Hide User List"
                                                        else "Show User List"
                                        ) {
                                                IconButton(
                                                        onClick = {
                                                                isUserListVisible =
                                                                        !isUserListVisible
                                                        }
                                                ) {
                                                        Icon(
                                                                imageVector =
                                                                        if (isUserListVisible)
                                                                                Icons.Default.People
                                                                        else
                                                                                Icons.Default
                                                                                        .PeopleOutline,
                                                                contentDescription =
                                                                        "Toggle User List",
                                                                tint =
                                                                        if (isUserListVisible)
                                                                                colors.cyan
                                                                        else colors.comment,
                                                                modifier = Modifier.size(20.dp)
                                                        )
                                                }
                                        }
                                        AppTooltip(text = "Join Channel") {
                                                IconButton(onClick = onJoinChannel) {
                                                        Icon(
                                                                imageVector = Icons.Default.Add,
                                                                contentDescription = "Join Channel",
                                                                tint = colors.green,
                                                                modifier = Modifier.size(20.dp)
                                                        )
                                                }
                                        }
                                        // Close pane button (only shown when onClosePane is
                                        // provided)
                                        if (onClosePane != null) {
                                                AppTooltip(text = "Close Pane") {
                                                        IconButton(onClick = onClosePane) {
                                                                Icon(
                                                                        imageVector =
                                                                                Icons.Default.Close,
                                                                        contentDescription =
                                                                                "Close Pane",
                                                                        tint = colors.red,
                                                                        modifier =
                                                                                Modifier.size(20.dp)
                                                                )
                                                        }
                                                }
                                        }
                                }
                        }
                }

                HorizontalDivider(color = colors.border, thickness = 1.dp)

                // Content (Chat + UserList)
                Row(modifier = Modifier.weight(1f)) {
                        // Chat Panel
                        Box(modifier = Modifier.weight(1f)) {
                                ChatPanel(
                                        serverId = serverId,
                                        channelName = channelName,
                                        isActive = isActive,
                                        markAsRead = markAsRead,
                                        onPaneActive = onPaneActive,
                                        connectionManager = connectionManager,
                                        userPreferences = userPreferences,
                                        onSendMessage = onSendMessage
                                )
                        }

                        // User List
                        if (isUserListVisible && channelUsers.isNotEmpty()) {
                                VerticalDivider(color = colors.border, thickness = 1.dp)

                                Column(
                                        modifier =
                                                Modifier.width(160.dp)
                                                        .fillMaxHeight()
                                                        .background(colors.windowBackground)
                                ) {
                                        Text(
                                                text = "Total: ${channelUsers.size}",
                                                style = MaterialTheme.typography.titleSmall,
                                                color = colors.foreground,
                                                modifier = Modifier.padding(8.dp)
                                        )

                                        val listState = rememberLazyListState()

                                        Box(modifier = Modifier.weight(1f)) {
                                                LazyColumn(
                                                        state = listState,
                                                        modifier = Modifier.fillMaxSize()
                                                ) {
                                                        val groupedUsers =
                                                                channelUsers
                                                                        .groupBy { user ->
                                                                                when {
                                                                                        user.modes
                                                                                                .contains(
                                                                                                        UserMode.OWNER
                                                                                                ) ->
                                                                                                0 to
                                                                                                        "~ Owners"
                                                                                        user.modes
                                                                                                .contains(
                                                                                                        UserMode.ADMIN
                                                                                                ) ->
                                                                                                1 to
                                                                                                        "& Admins"
                                                                                        user.modes
                                                                                                .contains(
                                                                                                        UserMode.OP
                                                                                                ) ->
                                                                                                2 to
                                                                                                        "@ Ops"
                                                                                        user.modes
                                                                                                .contains(
                                                                                                        UserMode.HALFOP
                                                                                                ) ->
                                                                                                3 to
                                                                                                        "% Half-Ops"
                                                                                        user.modes
                                                                                                .contains(
                                                                                                        UserMode.VOICE
                                                                                                ) ->
                                                                                                4 to
                                                                                                        "+ Voice"
                                                                                        else ->
                                                                                                5 to
                                                                                                        "Members"
                                                                                }
                                                                        }
                                                                        .toSortedMap(
                                                                                compareBy {
                                                                                        it.first
                                                                                }
                                                                        )

                                                        groupedUsers.forEach { (category, users) ->
                                                                stickyHeader {
                                                                        Surface(
                                                                                modifier =
                                                                                        Modifier.fillMaxWidth(),
                                                                                color =
                                                                                        colors.windowBackground
                                                                        ) {
                                                                                Text(
                                                                                        text =
                                                                                                "${category.second} — ${users.size}",
                                                                                        style =
                                                                                                MaterialTheme
                                                                                                        .typography
                                                                                                        .labelSmall,
                                                                                        fontWeight =
                                                                                                FontWeight
                                                                                                        .Bold,
                                                                                        color =
                                                                                                colors.comment,
                                                                                        modifier =
                                                                                                Modifier.padding(
                                                                                                        horizontal =
                                                                                                                8.dp,
                                                                                                        vertical =
                                                                                                                4.dp
                                                                                                )
                                                                                )
                                                                        }
                                                                }

                                                                items(
                                                                        users.sortedBy {
                                                                                it.nickname
                                                                                        .lowercase()
                                                                        }
                                                                ) { user ->
                                                                        Row(
                                                                                modifier =
                                                                                        Modifier.fillMaxWidth()
                                                                                                .clickable {
                                                                                                        onUserClick(
                                                                                                                user
                                                                                                        )
                                                                                                }
                                                                                                .padding(
                                                                                                        horizontal =
                                                                                                                8.dp,
                                                                                                        vertical =
                                                                                                                4.dp
                                                                                                ),
                                                                                verticalAlignment =
                                                                                        Alignment
                                                                                                .CenterVertically
                                                                        ) {
                                                                                val prefix =
                                                                                        when {
                                                                                                user.modes
                                                                                                        .contains(
                                                                                                                UserMode.OWNER
                                                                                                        ) ->
                                                                                                        "~"
                                                                                                user.modes
                                                                                                        .contains(
                                                                                                                UserMode.ADMIN
                                                                                                        ) ->
                                                                                                        "&"
                                                                                                user.modes
                                                                                                        .contains(
                                                                                                                UserMode.OP
                                                                                                        ) ->
                                                                                                        "@"
                                                                                                user.modes
                                                                                                        .contains(
                                                                                                                UserMode.HALFOP
                                                                                                        ) ->
                                                                                                        "%"
                                                                                                user.modes
                                                                                                        .contains(
                                                                                                                UserMode.VOICE
                                                                                                        ) ->
                                                                                                        "+"
                                                                                                else ->
                                                                                                        ""
                                                                                        }
                                                                                val color =
                                                                                        when {
                                                                                                user.modes
                                                                                                        .contains(
                                                                                                                UserMode.OWNER
                                                                                                        ) ->
                                                                                                        colors.red
                                                                                                user.modes
                                                                                                        .contains(
                                                                                                                UserMode.ADMIN
                                                                                                        ) ->
                                                                                                        colors.purple
                                                                                                user.modes
                                                                                                        .contains(
                                                                                                                UserMode.OP
                                                                                                        ) ->
                                                                                                        colors.red
                                                                                                user.modes
                                                                                                        .contains(
                                                                                                                UserMode.HALFOP
                                                                                                        ) ->
                                                                                                        colors.orange
                                                                                                user.modes
                                                                                                        .contains(
                                                                                                                UserMode.VOICE
                                                                                                        ) ->
                                                                                                        colors.yellow
                                                                                                else ->
                                                                                                        colors.foreground
                                                                                        }

                                                                                Text(
                                                                                        text =
                                                                                                "$prefix${user.nickname}",
                                                                                        color =
                                                                                                color,
                                                                                        style =
                                                                                                MaterialTheme
                                                                                                        .typography
                                                                                                        .bodySmall
                                                                                )
                                                                        }
                                                                }
                                                        }
                                                }

                                                CustomVerticalScrollbar(
                                                        listState = listState,
                                                        modifier =
                                                                Modifier.align(Alignment.CenterEnd)
                                                )
                                        }
                                }
                        }
                }
        }
}
