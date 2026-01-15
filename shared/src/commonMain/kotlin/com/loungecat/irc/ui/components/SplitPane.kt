package com.loungecat.irc.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp

/**
 * A horizontal split pane component that allows two panels to be displayed side-by-side with a
 * draggable divider.
 */
@Composable
fun SplitPane(
        modifier: Modifier = Modifier,
        initialSplitRatio: Float = 0.5f,
        minPanelWidth: Dp = 200.dp,
        dividerWidth: Dp = 4.dp,
        dividerColor: Color = Color.Gray.copy(alpha = 0.3f),
        leftPanel: @Composable () -> Unit,
        rightPanel: @Composable () -> Unit
) {
    var splitRatio by remember { mutableStateOf(initialSplitRatio) }
    val density = LocalDensity.current

    BoxWithConstraints(modifier = modifier) {
        val maxWidthPx = with(density) { maxWidth.toPx() }
        val minPanelWidthPx = with(density) { minPanelWidth.toPx() }
        val dividerWidthPx = with(density) { dividerWidth.toPx() }

        Row(modifier = Modifier.fillMaxSize()) {
            // Left panel
            Box(modifier = Modifier.fillMaxHeight().weight(splitRatio)) { leftPanel() }

            // Draggable divider
            Box(
                    modifier =
                            Modifier.fillMaxHeight()
                                    .width(dividerWidth)
                                    .background(dividerColor)
                                    .pointerInput(Unit) {
                                        detectDragGestures { change, dragAmount ->
                                            change.consume()
                                            val newRatio = splitRatio + (dragAmount.x / maxWidthPx)

                                            // Calculate min/max ratios based on panel constraints
                                            val minRatio = minPanelWidthPx / maxWidthPx
                                            val maxRatio =
                                                    1f - minRatio - (dividerWidthPx / maxWidthPx)

                                            splitRatio = newRatio.coerceIn(minRatio, maxRatio)
                                        }
                                    }
            )

            // Right panel
            Box(modifier = Modifier.fillMaxHeight().weight(1f - splitRatio)) { rightPanel() }
        }
    }
}

/** Enum for split layout orientation */
enum class SplitLayoutOrientation {
    HORIZONTAL, // Side by side
    VERTICAL // Stacked (top/bottom)
}

/** State holder for split/grid view functionality. */
class SplitViewState {
    var isEnabled by mutableStateOf(false)
        private set

    // Layout orientation for 2-pane mode
    var layoutOrientation by mutableStateOf(SplitLayoutOrientation.HORIZONTAL)
        private set

    // List of active channels (ServerID, ChannelName)
    private val _activeChannels = mutableStateListOf<Pair<Long, String>>()
    val activeChannels: List<Pair<Long, String>>
        get() = _activeChannels

    // Index of the currently active/focused pane
    var activePaneIndex by mutableStateOf(0)

    fun enable(channel1: Pair<Long, String>) {
        isEnabled = true
        _activeChannels.clear()
        _activeChannels.add(channel1)
        activePaneIndex = 0
    }

    fun disable() {
        isEnabled = false
        _activeChannels.clear()
        activePaneIndex = 0
    }

    fun toggleLayoutOrientation() {
        layoutOrientation =
                if (layoutOrientation == SplitLayoutOrientation.HORIZONTAL) {
                    SplitLayoutOrientation.VERTICAL
                } else {
                    SplitLayoutOrientation.HORIZONTAL
                }
    }

    fun addChannel(serverId: Long, channelName: String) {
        // Prevent duplicates
        if (_activeChannels.any { it.first == serverId && it.second == channelName }) {
            return
        }

        if (_activeChannels.size < 4) {
            _activeChannels.add(Pair(serverId, channelName))
            activePaneIndex = _activeChannels.lastIndex
        } else {
            // Replace active pane if full
            if (activePaneIndex in _activeChannels.indices) {
                _activeChannels[activePaneIndex] = Pair(serverId, channelName)
            }
        }
    }

    fun replaceActiveChannel(serverId: Long, channelName: String) {
        // Prevent duplicates (unless we are replacing the current one with itself, which is fine
        // but pointless)
        if (_activeChannels.any { it.first == serverId && it.second == channelName }) {
            return
        }

        if (activePaneIndex in _activeChannels.indices) {
            _activeChannels[activePaneIndex] = Pair(serverId, channelName)
        } else if (_activeChannels.isEmpty()) {
            addChannel(serverId, channelName)
        }
    }

    fun setActivePane(index: Int) {
        if (index in _activeChannels.indices) {
            activePaneIndex = index
        }
    }

    fun closeChannel(index: Int) {
        if (index in _activeChannels.indices) {
            _activeChannels.removeAt(index)
            // Adjust active pane index
            if (activePaneIndex >= _activeChannels.size) {
                activePaneIndex = (_activeChannels.size - 1).coerceAtLeast(0)
            }
            if (_activeChannels.isEmpty()) {
                isEnabled = false
            }
        }
    }

    fun swapChannels(fromIndex: Int, toIndex: Int) {
        if (fromIndex in _activeChannels.indices &&
                        toIndex in _activeChannels.indices &&
                        fromIndex != toIndex
        ) {
            val temp = _activeChannels[fromIndex]
            _activeChannels[fromIndex] = _activeChannels[toIndex]
            _activeChannels[toIndex] = temp
            // Update active pane to follow the swap if needed
            if (activePaneIndex == fromIndex) {
                activePaneIndex = toIndex
            } else if (activePaneIndex == toIndex) {
                activePaneIndex = fromIndex
            }
        }
    }
}

@Composable
fun rememberSplitViewState(): SplitViewState {
    return remember { SplitViewState() }
}
