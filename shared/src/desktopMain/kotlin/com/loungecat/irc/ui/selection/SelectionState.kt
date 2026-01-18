package com.loungecat.irc.ui.selection

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * Represents a selection anchor point within a specific message. Uses message key (UUID) for stable
 * identity across list mutations.
 */
data class SelectionBound(
        val messageKey: String, // IncomingMessage.id (UUID)
        val charOffset: Int // Character position within message content
)

/**
 * Holds the current selection state for the chat message list. State is hoisted to survive
 * LazyColumn item recycling.
 */
class SelectionState {
    /** Starting point of selection (where drag began) */
    var start: SelectionBound? by mutableStateOf(null)
        private set

    /** Ending point of selection (current drag position) */
    var end: SelectionBound? by mutableStateOf(null)
        private set

    /** Whether user is currently dragging to select */
    var isSelecting: Boolean by mutableStateOf(false)
        private set

    fun startSelection(bound: SelectionBound) {
        start = bound
        end = bound
        isSelecting = true
    }

    fun updateEnd(bound: SelectionBound) {
        end = bound
    }

    fun finishSelection() {
        isSelecting = false
    }

    fun clear() {
        start = null
        end = null
        isSelecting = false
    }

    fun hasSelection(): Boolean = start != null && end != null

    /**
     * Returns the bounds sorted by message index order (first message first). Call this with the
     * current message list to resolve which bound comes first.
     */
    fun getSortedBounds(messageKeys: List<String>): Pair<SelectionBound, SelectionBound>? {
        val s = start ?: return null
        val e = end ?: return null

        val startIndex = messageKeys.indexOf(s.messageKey)
        val endIndex = messageKeys.indexOf(e.messageKey)

        return when {
            startIndex < 0 || endIndex < 0 -> null
            startIndex < endIndex -> s to e
            startIndex > endIndex -> e to s
            // Same message - sort by character offset
            s.charOffset <= e.charOffset -> s to e
            else -> e to s
        }
    }
}
