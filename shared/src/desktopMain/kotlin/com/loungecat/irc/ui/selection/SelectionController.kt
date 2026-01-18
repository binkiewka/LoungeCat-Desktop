package com.loungecat.irc.ui.selection

import androidx.compose.foundation.lazy.LazyListState
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.text.TextLayoutResult
import com.loungecat.irc.data.model.IncomingMessage
import com.loungecat.irc.data.model.MessageType

/**
 * Controller for text selection across multiple messages in a LazyColumn. Handles coordinate
 * mapping, hit testing, and text extraction.
 */
class SelectionController(private val listState: LazyListState) {
    val state = SelectionState()

    /** Registry of TextLayoutResult for currently visible messages */
    val textLayoutRegistry = mutableStateMapOf<String, TextLayoutResult>()

    /** Current messages list - must be updated when messages change */
    private var messages: List<IncomingMessage> = emptyList()

    fun updateMessages(newMessages: List<IncomingMessage>) {
        messages = newMessages
    }

    fun registerTextLayout(messageKey: String, layout: TextLayoutResult) {
        textLayoutRegistry[messageKey] = layout
    }

    fun unregisterTextLayout(messageKey: String) {
        textLayoutRegistry.remove(messageKey)
    }

    /** Called when drag starts. Performs hit test to find starting character. */
    fun onDragStart(position: Offset, itemOffset: Int, messageKey: String): Boolean {
        val layout = textLayoutRegistry[messageKey] ?: return false

        // Adjust position relative to the text layout
        val localPosition = Offset(position.x, position.y - itemOffset)
        val charOffset = layout.getOffsetForPosition(localPosition).coerceAtLeast(0)

        state.startSelection(SelectionBound(messageKey, charOffset))
        return true
    }

    /** Called during drag. Updates the end bound of selection. */
    fun onDrag(position: Offset, itemOffset: Int, messageKey: String) {
        val layout = textLayoutRegistry[messageKey] ?: return

        val localPosition = Offset(position.x, position.y - itemOffset)
        val charOffset =
                layout.getOffsetForPosition(localPosition)
                        .coerceIn(0, getMessageText(messageKey).length)

        state.updateEnd(SelectionBound(messageKey, charOffset))
    }

    /** Called when drag ends. */
    fun onDragEnd() {
        state.finishSelection()
    }

    /** Clears the current selection. */
    fun clearSelection() {
        state.clear()
    }

    /** Extracts the selected text as a plain string. */
    fun getSelectedText(): String {
        val messageKeys = messages.map { it.id }
        val (startBound, endBound) = state.getSortedBounds(messageKeys) ?: return ""

        val startIdx = messageKeys.indexOf(startBound.messageKey)
        val endIdx = messageKeys.indexOf(endBound.messageKey)

        if (startIdx < 0 || endIdx < 0) return ""

        return buildString {
            for (i in startIdx..endIdx) {
                val msg = messages[i]
                val text = getFormattedMessageText(msg)

                when (i) {
                    startIdx -> {
                        if (i == endIdx) {
                            // Selection within single message
                            val start = startBound.charOffset.coerceIn(0, text.length)
                            val end = endBound.charOffset.coerceIn(0, text.length)
                            append(text.substring(start, end))
                        } else {
                            // First message - from start offset to end
                            val start = startBound.charOffset.coerceIn(0, text.length)
                            append(text.substring(start))
                            append("\n")
                        }
                    }
                    endIdx -> {
                        // Last message - from beginning to end offset
                        val end = endBound.charOffset.coerceIn(0, text.length)
                        append(text.substring(0, end))
                    }
                    else -> {
                        // Middle messages - full text
                        append(text)
                        append("\n")
                    }
                }
            }
        }
    }

    /** Checks if a message is part of the current selection. */
    fun isMessageSelected(messageKey: String): Boolean {
        val messageKeys = messages.map { it.id }
        val (startBound, endBound) = state.getSortedBounds(messageKeys) ?: return false

        val msgIdx = messageKeys.indexOf(messageKey)
        val startIdx = messageKeys.indexOf(startBound.messageKey)
        val endIdx = messageKeys.indexOf(endBound.messageKey)

        return msgIdx in startIdx..endIdx
    }

    /**
     * Gets selection range for a specific message (character offsets). Returns null if message is
     * not selected.
     */
    fun getSelectionRangeForMessage(messageKey: String): IntRange? {
        val messageKeys = messages.map { it.id }
        val (startBound, endBound) = state.getSortedBounds(messageKeys) ?: return null

        val msgIdx = messageKeys.indexOf(messageKey)
        val startIdx = messageKeys.indexOf(startBound.messageKey)
        val endIdx = messageKeys.indexOf(endBound.messageKey)

        if (msgIdx !in startIdx..endIdx) return null

        val text = getMessageText(messageKey)

        return when (msgIdx) {
            startIdx -> {
                if (msgIdx == endIdx) {
                    // Single message selection
                    startBound.charOffset..endBound.charOffset
                } else {
                    // First message
                    startBound.charOffset..text.length
                }
            }
            endIdx -> {
                // Last message
                0..endBound.charOffset
            }
            else -> {
                // Middle message - fully selected
                0..text.length
            }
        }
    }

    /** Gets the rectangles to draw for selection highlight on a specific message. */
    fun getSelectionRectsForMessage(messageKey: String): List<Rect> {
        val range = getSelectionRangeForMessage(messageKey) ?: return emptyList()
        val layout = textLayoutRegistry[messageKey] ?: return emptyList()

        val start = range.first.coerceIn(0, layout.layoutInput.text.length)
        val end = range.last.coerceIn(0, layout.layoutInput.text.length)

        if (start >= end) return emptyList()

        return layout.getPathForRange(start, end).getBounds().let { bounds ->
            if (bounds.isEmpty) emptyList() else listOf(bounds)
        }
    }

    private fun getMessageText(messageKey: String): String {
        val msg = messages.find { it.id == messageKey } ?: return ""
        return getFormattedMessageText(msg)
    }

    private fun getFormattedMessageText(msg: IncomingMessage): String {
        return when (msg.type) {
            MessageType.ACTION -> "* ${msg.sender} ${msg.content}"
            MessageType.JOIN -> "-> ${msg.sender} has joined"
            MessageType.PART, MessageType.QUIT -> "<- ${msg.sender} has left: ${msg.content}"
            MessageType.NICK -> "<> ${msg.sender} ${msg.content}"
            else -> msg.content
        }
    }
}
