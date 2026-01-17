package com.loungecat.irc.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.SwapHoriz
import androidx.compose.material.icons.filled.SwapVert
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import com.loungecat.irc.data.model.ChannelUser
import com.loungecat.irc.data.model.UserPreferences
import com.loungecat.irc.service.DesktopConnectionManager
import com.loungecat.irc.ui.theme.AppColors

@Composable
fun ChatGrid(
        splitViewState: SplitViewState,
        connectionManager: DesktopConnectionManager,
        userPreferences: UserPreferences,
        onSendMessage: (Long, String, String) -> Unit,
        onJoinChannel: (Long) -> Unit = {},
        onUserClick: (ChannelUser) -> Unit = {}
) {
        val activeChannels = splitViewState.activeChannels
        val activePaneIndex = splitViewState.activePaneIndex
        val layoutOrientation = splitViewState.layoutOrientation
        val colors = AppColors.current

        // Drag state for reordering
        var draggedIndex by remember { mutableStateOf<Int?>(null) }
        var dragOffset by remember { mutableStateOf(0f) }

        if (activeChannels.isEmpty()) return

        // Helper composable for a single pane wrapper with drag support
        @Composable
        fun GridPane(index: Int, modifier: Modifier = Modifier, enableDrag: Boolean = false) {
                val (serverId, channelName) = activeChannels[index]
                val isActive = index == activePaneIndex
                val isDragging = draggedIndex == index

                // Lifted effect for active pane: shadow + subtle background highlight + zIndex
                val paneModifier =
                        if (isActive) {
                                modifier.zIndex(if (isDragging) 2f else 1f)
                        } else {
                                modifier.zIndex(if (isDragging) 2f else 0f)
                        }

                // Apply drag offset for visual feedback
                val finalModifier =
                        if (isDragging) {
                                paneModifier.graphicsLayer {
                                        translationY = dragOffset
                                        alpha = 0.8f
                                        scaleX = 0.4f
                                        scaleY = 0.4f
                                }
                        } else {
                                paneModifier
                        }

                Box(
                        modifier =
                                finalModifier
                                        .fillMaxSize()
                                        .clickable { splitViewState.setActivePane(index) }
                                        .then(
                                                if (enableDrag && activeChannels.size > 2) {
                                                        Modifier.pointerInput(Unit) {
                                                                detectDragGestures(
                                                                        onDragStart = {
                                                                                draggedIndex = index
                                                                                dragOffset = 0f
                                                                        },
                                                                        onDragEnd = {
                                                                                // Calculate target
                                                                                // index based on
                                                                                // drag distance
                                                                                val direction =
                                                                                        if (dragOffset >
                                                                                                        50
                                                                                        )
                                                                                                1
                                                                                        else if (dragOffset <
                                                                                                        -50
                                                                                        )
                                                                                                -1
                                                                                        else 0
                                                                                val targetIndex =
                                                                                        (index +
                                                                                                        direction)
                                                                                                .coerceIn(
                                                                                                        0,
                                                                                                        activeChannels
                                                                                                                .size -
                                                                                                                1
                                                                                                )
                                                                                if (targetIndex !=
                                                                                                index
                                                                                ) {
                                                                                        splitViewState
                                                                                                .swapChannels(
                                                                                                        index,
                                                                                                        targetIndex
                                                                                                )
                                                                                }
                                                                                draggedIndex = null
                                                                                dragOffset = 0f
                                                                        },
                                                                        onDragCancel = {
                                                                                draggedIndex = null
                                                                                dragOffset = 0f
                                                                        },
                                                                        onDrag = {
                                                                                change,
                                                                                dragAmount ->
                                                                                change.consume()
                                                                                dragOffset +=
                                                                                        dragAmount.y
                                                                        }
                                                                )
                                                        }
                                                } else Modifier
                                        )
                ) {
                        ChannelPane(
                                serverId = serverId,
                                channelName = channelName,
                                isActive = isActive,
                                connectionManager = connectionManager,
                                userPreferences = userPreferences,
                                onSendMessage = { message ->
                                        onSendMessage(serverId, channelName, message)
                                },
                                onJoinChannel = { onJoinChannel(serverId) },
                                // Show close button when there are 2+ panes
                                onClosePane =
                                        if (activeChannels.size > 1) {
                                                { splitViewState.closeChannel(index) }
                                        } else null,
                                onUserClick = onUserClick
                        )
                }
        }

        // Divider composable for reuse
        @Composable
        fun VerticalDivider() {
                Box(
                        modifier =
                                Modifier.width(4.dp)
                                        .fillMaxHeight()
                                        .background(Color.Gray.copy(alpha = 0.3f))
                )
        }

        @Composable
        fun HorizontalDivider() {
                Box(
                        modifier =
                                Modifier.height(4.dp)
                                        .fillMaxWidth()
                                        .background(Color.Gray.copy(alpha = 0.3f))
                )
        }

        Column(modifier = Modifier.fillMaxSize()) {
                // Layout toggle toolbar (only for 2-pane mode)
                if (activeChannels.size == 2) {
                        Row(
                                modifier =
                                        Modifier.fillMaxWidth()
                                                .background(colors.windowBackground)
                                                .padding(horizontal = 8.dp, vertical = 2.dp),
                                horizontalArrangement = Arrangement.End,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                                IconButton(
                                        onClick = { splitViewState.toggleLayoutOrientation() },
                                        modifier = Modifier.size(28.dp)
                                ) {
                                        Icon(
                                                imageVector =
                                                        if (layoutOrientation ==
                                                                        SplitLayoutOrientation
                                                                                .HORIZONTAL
                                                        )
                                                                Icons.Default.SwapVert
                                                        else Icons.Default.SwapHoriz,
                                                contentDescription = "Toggle Layout",
                                                tint = colors.cyan,
                                                modifier = Modifier.size(18.dp)
                                        )
                                }
                        }
                }

                // Grid content
                Box(modifier = Modifier.weight(1f)) {
                        when (activeChannels.size) {
                                1 -> {
                                        val (serverId, channelName) = activeChannels[0]
                                        key(serverId, channelName) { GridPane(0) }
                                }
                                2 -> {
                                        if (layoutOrientation == SplitLayoutOrientation.HORIZONTAL
                                        ) {
                                                // Side by side
                                                Row(modifier = Modifier.fillMaxSize()) {
                                                        val (s1, c1) = activeChannels[0]
                                                        key(s1, c1) {
                                                                GridPane(0, Modifier.weight(1f))
                                                        }
                                                        VerticalDivider()
                                                        val (s2, c2) = activeChannels[1]
                                                        key(s2, c2) {
                                                                GridPane(1, Modifier.weight(1f))
                                                        }
                                                }
                                        } else {
                                                // Stacked (top/bottom)
                                                Column(modifier = Modifier.fillMaxSize()) {
                                                        val (s1, c1) = activeChannels[0]
                                                        key(s1, c1) {
                                                                GridPane(0, Modifier.weight(1f))
                                                        }
                                                        HorizontalDivider()
                                                        val (s2, c2) = activeChannels[1]
                                                        key(s2, c2) {
                                                                GridPane(1, Modifier.weight(1f))
                                                        }
                                                }
                                        }
                                }
                                3 -> {
                                        // 3 items: 2 on top, 1 on bottom (with drag support)
                                        Column(modifier = Modifier.fillMaxSize()) {
                                                Row(modifier = Modifier.weight(1f)) {
                                                        val (s1, c1) = activeChannels[0]
                                                        key(s1, c1) {
                                                                GridPane(
                                                                        0,
                                                                        Modifier.weight(1f),
                                                                        enableDrag = true
                                                                )
                                                        }
                                                        VerticalDivider()
                                                        val (s2, c2) = activeChannels[1]
                                                        key(s2, c2) {
                                                                GridPane(
                                                                        1,
                                                                        Modifier.weight(1f),
                                                                        enableDrag = true
                                                                )
                                                        }
                                                }
                                                HorizontalDivider()
                                                Row(modifier = Modifier.weight(1f)) {
                                                        val (s3, c3) = activeChannels[2]
                                                        key(s3, c3) {
                                                                GridPane(
                                                                        2,
                                                                        Modifier.weight(1f),
                                                                        enableDrag = true
                                                                )
                                                        }
                                                }
                                        }
                                }
                                4 -> {
                                        // 4 items: 2x2 (with drag support)
                                        Column(modifier = Modifier.fillMaxSize()) {
                                                Row(modifier = Modifier.weight(1f)) {
                                                        val (s1, c1) = activeChannels[0]
                                                        key(s1, c1) {
                                                                GridPane(
                                                                        0,
                                                                        Modifier.weight(1f),
                                                                        enableDrag = true
                                                                )
                                                        }
                                                        VerticalDivider()
                                                        val (s2, c2) = activeChannels[1]
                                                        key(s2, c2) {
                                                                GridPane(
                                                                        1,
                                                                        Modifier.weight(1f),
                                                                        enableDrag = true
                                                                )
                                                        }
                                                }
                                                HorizontalDivider()
                                                Row(modifier = Modifier.weight(1f)) {
                                                        val (s3, c3) = activeChannels[2]
                                                        key(s3, c3) {
                                                                GridPane(
                                                                        2,
                                                                        Modifier.weight(1f),
                                                                        enableDrag = true
                                                                )
                                                        }
                                                        VerticalDivider()
                                                        val (s4, c4) = activeChannels[3]
                                                        key(s4, c4) {
                                                                GridPane(
                                                                        3,
                                                                        Modifier.weight(1f),
                                                                        enableDrag = true
                                                                )
                                                        }
                                                }
                                        }
                                }
                        }
                }
        }
}
