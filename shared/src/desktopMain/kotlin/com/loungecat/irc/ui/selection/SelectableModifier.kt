package com.loungecat.irc.ui.selection

import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.composed
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextLayoutResult

/**
 * Modifier that enables a message item to participate in cross-item selection. Handles
 * TextLayoutResult registration and selection highlight drawing.
 */
fun Modifier.selectableMessage(
        messageKey: String,
        controller: SelectionController,
        highlightColor: Color
): Modifier = composed {
    var textLayoutResult: TextLayoutResult? by remember { mutableStateOf(null) }

    // Cleanup when composable leaves composition
    DisposableEffect(messageKey) { onDispose { controller.unregisterTextLayout(messageKey) } }

    this.drawBehind {
        val selectionRects = controller.getSelectionRectsForMessage(messageKey)
        selectionRects.forEach { rect ->
            drawRect(color = highlightColor, topLeft = rect.topLeft, size = rect.size)
        }
    }
}

/** Extension to create an onTextLayout callback that registers with the controller. */
fun SelectionController.createTextLayoutCallback(messageKey: String): (TextLayoutResult) -> Unit {
    return { result -> registerTextLayout(messageKey, result) }
}
