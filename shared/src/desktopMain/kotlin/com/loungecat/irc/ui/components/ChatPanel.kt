@file:Suppress("DEPRECATION")

package com.loungecat.irc.ui.components

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Send
import androidx.compose.material.icons.filled.Image
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.input.key.*
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.onPointerEvent
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.input.TextFieldValue
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.zIndex
import com.loungecat.irc.data.model.JoinPartQuitDisplayMode
import com.loungecat.irc.data.model.MessageType
import com.loungecat.irc.data.model.UserPreferences
import com.loungecat.irc.service.DesktopConnectionManager
import com.loungecat.irc.shared.generated.resources.Res
import com.loungecat.irc.shared.generated.resources.logo_transparent
import com.loungecat.irc.ui.selection.SelectionController
import com.loungecat.irc.ui.theme.AppColors
import com.loungecat.irc.ui.util.ChatUiItem
import com.loungecat.irc.ui.util.flattenToMessages
import com.loungecat.irc.ui.util.groupMessages
import com.loungecat.irc.util.InputHistoryHelper
import com.loungecat.irc.util.Logger
import com.loungecat.irc.util.SpellCheckRuleMatch
import com.loungecat.irc.util.SpellChecker
import com.loungecat.irc.util.TabCompletionHelper
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.jetbrains.compose.resources.painterResource

@OptIn(ExperimentalComposeUiApi::class)
@Composable
fun ChatPanel(
        serverId: Long,
        channelName: String,
        isActive: Boolean = false,
        connectionManager: DesktopConnectionManager,
        userPreferences: UserPreferences,
        onSendMessage: (String) -> Unit
) {
    val colors = AppColors.current
    val scope = rememberCoroutineScope()

    // State
    // State
    val connectionStates by connectionManager.connectionStates.collectAsState()
    val messages = connectionStates[serverId]?.messages ?: emptyMap()
    val urlPreviews by connectionManager.urlPreviews.collectAsState()
    val imageUrls by connectionManager.imageUrls.collectAsState()
    val whoisCache by connectionManager.whoisCache.collectAsState()

    val joinPartQuitMode = userPreferences.joinPartQuitMode
    val smartHideMinutes = userPreferences.smartHideMinutes
    val activityTracker = connectionManager.userActivityTracker

    val channelMessages =
            remember(messages, channelName, joinPartQuitMode, smartHideMinutes) {
                // Filter out TOPIC messages as they are now displayed in the channel bar header
                val allMessages =
                        (messages[channelName] ?: emptyList()).filter {
                            it.type != MessageType.TOPIC
                        }
                val jpqTypes = listOf(MessageType.JOIN, MessageType.PART, MessageType.QUIT)

                when (joinPartQuitMode) {
                    JoinPartQuitDisplayMode.SHOW_ALL -> groupMessages(allMessages)
                    JoinPartQuitDisplayMode.HIDE_ALL ->
                            groupMessages(allMessages.filter { it.type !in jpqTypes })
                    JoinPartQuitDisplayMode.GROUPED -> {
                        groupMessages(allMessages)
                    }
                    JoinPartQuitDisplayMode.SMART_HIDE -> {
                        val filtered =
                                allMessages.filter { msg ->
                                    if (msg.type in jpqTypes) {
                                        // Only show if user was recently active
                                        activityTracker.wasRecentlyActive(
                                                channelName,
                                                msg.sender,
                                                smartHideMinutes
                                        )
                                    } else {
                                        true
                                    }
                                }
                        groupMessages(filtered)
                    }
                }
            }

    var messageInput by remember { mutableStateOf(TextFieldValue("")) }
    var showSpellCheckMenu by remember { mutableStateOf(false) }
    var spellCheckSuggestions by remember { mutableStateOf<List<String>>(emptyList()) }
    var previousText by remember { mutableStateOf("") }
    var isKeyboardNavigation by remember { mutableStateOf(false) }

    // Store current spell check errors (stateless check)
    var spellCheckErrors by remember { mutableStateOf<List<SpellCheckRuleMatch>>(emptyList()) }

    val tabCompletionHelper = remember { TabCompletionHelper() }
    val inputHistoryHelper = remember { InputHistoryHelper(userPreferences.inputHistorySize) }
    val focusRequester = remember { FocusRequester() }

    // Text selection support
    val clipboardManager = LocalClipboardManager.current
    val listState = rememberLazyListState()
    val selectionController = remember(listState) { SelectionController(listState) }

    // Update selection controller with current messages
    LaunchedEffect(channelMessages) {
        selectionController.updateMessages(channelMessages.flattenToMessages())
    }

    LaunchedEffect(isActive) {
        if (isActive) {
            focusRequester.requestFocus()
        }
    }

    // Helper to generate annotated string with spell check highlights
    val getAnnotatedString =
            remember(userPreferences.spellCheckEnabled, colors, spellCheckErrors) {
                { text: String ->
                    if (userPreferences.spellCheckEnabled) {
                        buildAnnotatedString {
                            append(text)
                            spellCheckErrors.forEach { match ->
                                // Ensure range is valid for current text
                                val start = match.range.first.coerceIn(0, text.length)
                                val end = (match.range.last + 1).coerceIn(0, text.length)

                                if (start < end) {
                                    addStyle(
                                            SpanStyle(
                                                    color = colors.red,
                                                    textDecoration = TextDecoration.Underline
                                            ),
                                            start,
                                            end
                                    )
                                }
                            }
                        }
                    } else {
                        AnnotatedString(text)
                    }
                }
            }

    // Async spell check: debounced processing of full text
    LaunchedEffect(messageInput.text, userPreferences.spellCheckEnabled) {
        if (!userPreferences.spellCheckEnabled || messageInput.text.isEmpty()) {
            spellCheckErrors = emptyList()
            return@LaunchedEffect
        }

        // Debounce to avoid checking on every keystroke
        delay(300)

        // Check full text and update local state
        spellCheckErrors = SpellChecker.checkText(messageInput.text)
    }

    // Update annotations when spell check errors update
    LaunchedEffect(spellCheckErrors) {
        val annotatedString = getAnnotatedString(messageInput.text)
        if (messageInput.annotatedString != annotatedString) {
            messageInput = messageInput.copy(annotatedString = annotatedString)
        }
    }

    // Detect clicks on misspelled words
    LaunchedEffect(messageInput.selection, messageInput.text) {
        val textChanged = messageInput.text != previousText
        previousText = messageInput.text

        // Only show suggestions on click (selection change without text change)
        if (!textChanged && userPreferences.spellCheckEnabled && messageInput.text.isNotEmpty()) {
            val cursor = messageInput.selection.start
            com.loungecat.irc.util.Logger.d("ChatPanel", "Click detected at cursor: $cursor")

            // Find error match at cursor
            val match =
                    spellCheckErrors.find {
                        cursor >= it.range.first && cursor <= (it.range.last + 1)
                    }

            if (match != null && !isKeyboardNavigation) {
                com.loungecat.irc.util.Logger.d(
                        "ChatPanel",
                        "Match found: ${match.message}, suggestions: ${match.suggestions.size}"
                )
                showSpellCheckMenu = true
                spellCheckSuggestions = match.suggestions
            } else {
                com.loungecat.irc.util.Logger.d(
                        "ChatPanel",
                        "No match found at cursor $cursor (or keyboard nav: $isKeyboardNavigation). Errors: ${spellCheckErrors.size}"
                )
                showSpellCheckMenu = false
            }
        } else if (textChanged) {
            // Hide menu when typing
            showSpellCheckMenu = false
        }
    }

    // Need current users for tab completion
    // We can get them from connectionManager via a helper or passing them in.
    // Ideally ConnectionManager exposes a way to get users for a channel.
    // existing method: connectionManager.getChannelUsers(channelName) (but it uses
    // _currentServerId)
    // We should use a method that takes serverId and channelName.
    // I added/fix getChannelUsers to use channelName, but it relies on _currentServerId.
    // I should probably fix that or pass users in.
    // For now, let's assume getChannelUsers is sufficient if we ensure currentServerId is correct
    // or...
    // Actually in Split View, one panel might not be "current".
    // I need a way to get users for ANY channel.
    // DesktopConnectionManager has connections[serverId].client.channels...
    // I can expose a flow or just access it.
    // connectionManager.getChannelUsers(channelName) relies on _currentServerId.
    // I'll add getChannelUsers(serverId, channelName) to DesktopConnectionManager later.
    // For now, I'll access it via a safe call or just pass empty list if not available,
    // but tab completion needs it.

    Box(
            modifier =
                    Modifier.fillMaxSize()
                            .zIndex(1f) // Ensure it draws above side panels
                            .shadow(
                                    elevation = 12.dp,
                                    shape = androidx.compose.ui.graphics.RectangleShape
                            )
                            .background(colors.background)
            // TODO: Re-enable drag-and-drop when Compose Multiplatform provides
            // non-deprecated API (Modifier.dragAndDropTarget)
            ) {
        Image(
                painter = painterResource(Res.drawable.logo_transparent),
                contentDescription = null,
                modifier = Modifier.align(Alignment.Center).fillMaxSize(0.6f),
                alpha = 0.15f
        )

        Column(modifier = Modifier.fillMaxSize()) {
            val isLoadingOlder by connectionManager.isLoadingOlderMessages.collectAsState()

            // Auto-scroll logic
            var hasInitialScrolled by remember(channelName) { mutableStateOf(false) }

            // Mark as read when active
            LaunchedEffect(isActive, channelMessages.size) {
                if (isActive) {
                    connectionManager.markAsRead(serverId, channelName)
                }
            }

            LaunchedEffect(channelMessages.size, channelName) {
                if (channelMessages.isNotEmpty()) {
                    if (!hasInitialScrolled) {
                        try {
                            delay(50) // Wait for layout to settle, critical for Windows
                            listState.scrollToItem(channelMessages.size - 1)
                            hasInitialScrolled = true
                        } catch (e: Exception) {
                            // Ignore cancellation
                        }
                    } else if (!isLoadingOlder) {
                        val lastItem = channelMessages.last()

                        val isSelf =
                                when (lastItem) {
                                    is ChatUiItem.SingleMessage -> lastItem.message.isSelf
                                    is ChatUiItem.GroupedEvents -> lastItem.events.any { it.isSelf }
                                }

                        // Check if we are currently at the bottom (or close to it)
                        val layoutInfo = listState.layoutInfo
                        val totalItemsCount = layoutInfo.totalItemsCount
                        val lastVisibleItemIndex =
                                layoutInfo.visibleItemsInfo.lastOrNull()?.index ?: 0

                        // If the user was viewing the previous last item, they are "at bottom"
                        // Allow a margin of error (e.g. 3 items for better tolerance)
                        val isAtBottom =
                                lastVisibleItemIndex >= totalItemsCount - 3 || totalItemsCount <= 1

                        if (isAtBottom || isSelf) {
                            try {
                                listState.animateScrollToItem(channelMessages.size - 1)
                            } catch (e: Exception) {
                                listState.scrollToItem(channelMessages.size - 1)
                            }
                        }
                    }
                }
            }

            // Detect scroll to top for persistent scrollback
            LaunchedEffect(listState) {
                snapshotFlow { listState.firstVisibleItemIndex }.collect { firstVisibleIndex ->
                    if (firstVisibleIndex == 0 && channelMessages.isNotEmpty() && !isLoadingOlder) {
                        val loadedCount = connectionManager.loadOlderMessages(serverId, channelName)
                        // Adjust scroll position to maintain view
                        if (loadedCount > 0) {
                            listState.scrollToItem(loadedCount)
                        }
                    }
                }
            }

            Row(modifier = Modifier.weight(1f)) {
                LazyColumn(
                        state = listState,
                        modifier =
                                Modifier.weight(1f)
                                        .padding(8.dp)
                                        .onPointerEvent(PointerEventType.Press) { event ->
                                            // Start selection on click
                                            val position = event.changes.firstOrNull()?.position
                                            if (position != null) {
                                                findMessageAtPosition(
                                                                position,
                                                                listState,
                                                                channelMessages
                                                        )
                                                        ?.let { (messageKey, localY) ->
                                                            selectionController.onDragStart(
                                                                    Offset(position.x, localY),
                                                                    0,
                                                                    messageKey
                                                            )
                                                        }
                                            }
                                        }
                                        .onPointerEvent(PointerEventType.Move) { event ->
                                            // Continue selection if active and button still pressed
                                            val isPressed = event.changes.any { it.pressed }
                                            if (selectionController.state.isSelecting && isPressed
                                            ) {
                                                val position = event.changes.firstOrNull()?.position
                                                if (position != null) {
                                                    findMessageAtPosition(
                                                                    position,
                                                                    listState,
                                                                    channelMessages
                                                            )
                                                            ?.let { (messageKey, localY) ->
                                                                selectionController.onDrag(
                                                                        Offset(position.x, localY),
                                                                        0,
                                                                        messageKey
                                                                )
                                                            }
                                                }
                                            }
                                        }
                                        .onPointerEvent(PointerEventType.Release) { _ ->
                                            if (selectionController.state.isSelecting) {
                                                selectionController.onDragEnd()
                                            }
                                        }
                ) {
                    items(items = channelMessages, key = { it.id }) { item ->
                        when (item) {
                            is ChatUiItem.SingleMessage -> {
                                val message = item.message
                                MessageItem(
                                        message = message,
                                        urlPreviews = urlPreviews[message.id] ?: emptyList(),
                                        imageUrls = imageUrls[message.id] ?: emptyList(),
                                        currentNickname =
                                                connectionManager.getConnection(serverId)
                                                        ?.config
                                                        ?.nickname
                                                        ?: "",
                                        urlImageDisplayMode = userPreferences.urlImageDisplayMode,
                                        fontSize = userPreferences.fontSize,
                                        timestampFormat = userPreferences.timestampFormat,
                                        coloredNicknames = userPreferences.coloredNicknames,
                                        whoisInfo = whoisCache,
                                        onRequestWhois = { nick ->
                                            connectionManager.requestSilentWhois(serverId, nick)
                                        },
                                        selectionController = selectionController,
                                        selectionHighlightColor =
                                                colors.selection.copy(alpha = 0.4f),
                                        onCopySelection = { text ->
                                            clipboardManager.setText(AnnotatedString(text))
                                            selectionController.clearSelection()
                                        },
                                        onLoadPreview = { msgId ->
                                            connectionManager.fetchPreviewForMessage(msgId)
                                        }
                                )
                            }
                            is ChatUiItem.GroupedEvents -> {
                                GroupedEventItem(item)
                            }
                        }
                    }
                }
                CustomVerticalScrollbar(listState = listState)
            }

            HorizontalDivider(color = colors.border, thickness = 1.dp)

            Row(
                    modifier =
                            Modifier.fillMaxWidth()
                                    .background(colors.windowBackground)
                                    .padding(12.dp),
                    verticalAlignment = Alignment.CenterVertically
            ) {
                Box(modifier = Modifier.weight(1f)) {
                    BasicTextField(
                            value = messageInput,
                            onValueChange = { newValue ->
                                // Reset tab completion on any input change
                                if (newValue.text != messageInput.text) {
                                    tabCompletionHelper.reset()
                                }

                                // Apply spell check styling
                                val annotatedString = getAnnotatedString(newValue.text)
                                messageInput = newValue.copy(annotatedString = annotatedString)
                            },
                            modifier =
                                    Modifier.fillMaxWidth()
                                            .background(colors.background, RoundedCornerShape(8.dp))
                                            .focusRequester(focusRequester)
                                            .then(
                                                    if (isActive) {
                                                        Modifier.border(
                                                                width = 2.dp,
                                                                color = colors.cyan,
                                                                shape = RoundedCornerShape(8.dp)
                                                        )
                                                    } else Modifier
                                            )
                                            .padding(
                                                    start = 12.dp,
                                                    top = 12.dp,
                                                    bottom = 12.dp,
                                                    end = 48.dp
                                            )
                                            .onPointerEvent(PointerEventType.Press) {
                                                isKeyboardNavigation = false
                                            }
                                            .onPreviewKeyEvent { event ->
                                                if (event.type == KeyEventType.KeyDown) {
                                                    isKeyboardNavigation = true
                                                }
                                                when {
                                                    // Tab completion
                                                    event.key == Key.Tab &&
                                                            event.type == KeyEventType.KeyDown -> {
                                                        // Fetch users for completion only when
                                                        // needed
                                                        val currentUsers =
                                                                connectionManager
                                                                        .getConnection(serverId)
                                                                        ?.channels
                                                                        ?.find {
                                                                            it.name.equals(
                                                                                    channelName,
                                                                                    ignoreCase =
                                                                                            true
                                                                            )
                                                                        }
                                                                        ?.users
                                                                        ?: emptyList()

                                                        val channels =
                                                                connectionManager.getConnection(
                                                                                serverId
                                                                        )
                                                                        ?.channels
                                                                        ?.map { it.name }
                                                                        ?: emptyList()

                                                        val isAtStart =
                                                                messageInput.text.isEmpty() ||
                                                                        !messageInput.text.contains(
                                                                                " "
                                                                        )

                                                        if (!tabCompletionHelper.isActive()) {
                                                            // Start new completion
                                                            tabCompletionHelper.initCompletion(
                                                                    messageInput.text,
                                                                    messageInput
                                                                            .selection
                                                                            .start, // Use cursor
                                                                    // position
                                                                    currentUsers,
                                                                    channels,
                                                                    isAtStart
                                                            )
                                                        }

                                                        val completed =
                                                                if (event.isShiftPressed) {
                                                                    tabCompletionHelper
                                                                            .cyclePrevious()
                                                                } else {
                                                                    tabCompletionHelper.cycleNext()
                                                                }

                                                        completed?.let {
                                                            // Update text and move cursor to end
                                                            // (simplification)
                                                            val newCursorPos =
                                                                    tabCompletionHelper
                                                                            .getNewCursorPosition(
                                                                                    it
                                                                            )
                                                            messageInput =
                                                                    TextFieldValue(
                                                                            it,
                                                                            TextRange(newCursorPos)
                                                                    )
                                                        }
                                                        true
                                                    }
                                                    // Enter to send
                                                    event.key == Key.Enter &&
                                                            event.type == KeyEventType.KeyDown &&
                                                            !event.isShiftPressed -> {
                                                        if (messageInput.text.isNotBlank()) {
                                                            // Add to input history before sending
                                                            inputHistoryHelper.addMessage(
                                                                    channelName,
                                                                    messageInput.text
                                                            )
                                                            onSendMessage(messageInput.text)
                                                            messageInput = TextFieldValue("")
                                                            tabCompletionHelper.reset()
                                                        }
                                                        true
                                                    }
                                                    // Arrow Up - navigate history backward
                                                    event.key == Key.DirectionUp &&
                                                            event.type == KeyEventType.KeyDown -> {
                                                        val historyMessage =
                                                                inputHistoryHelper.navigateUp(
                                                                        channelName,
                                                                        messageInput.text
                                                                )
                                                        historyMessage?.let {
                                                            messageInput =
                                                                    TextFieldValue(
                                                                            it,
                                                                            TextRange(it.length)
                                                                    )
                                                        }
                                                        true
                                                    }
                                                    // Arrow Down - navigate history forward
                                                    event.key == Key.DirectionDown &&
                                                            event.type == KeyEventType.KeyDown -> {
                                                        val historyMessage =
                                                                inputHistoryHelper.navigateDown(
                                                                        channelName
                                                                )
                                                        historyMessage?.let {
                                                            messageInput =
                                                                    TextFieldValue(
                                                                            it,
                                                                            TextRange(it.length)
                                                                    )
                                                        }
                                                        true
                                                    }
                                                    // Ctrl+M for Quick Fix (Fix All)
                                                    event.key == Key.M &&
                                                            event.type == KeyEventType.KeyDown &&
                                                            event.isCtrlPressed -> {
                                                        com.loungecat.irc.util.Logger.d(
                                                                "ChatPanel",
                                                                "Ctrl+M pressed"
                                                        )
                                                        scope.launch {
                                                            val currentText = messageInput.text
                                                            if (currentText.isNotBlank()) {
                                                                val immediateErrors =
                                                                        SpellChecker.checkText(
                                                                                currentText
                                                                        )
                                                                com.loungecat.irc.util.Logger.d(
                                                                        "ChatPanel",
                                                                        "Ctrl+M found ${immediateErrors.size} errors"
                                                                )

                                                                if (immediateErrors.isNotEmpty()) {
                                                                    var newText = currentText
                                                                    var formattedTextChange = false
                                                                    // ... (logic continues)

                                                                    // Apply changes from back to
                                                                    // front to
                                                                    // preserve indices
                                                                    val sortedErrors =
                                                                            immediateErrors
                                                                                    .sortedByDescending {
                                                                                        it.range
                                                                                                .first
                                                                                    }

                                                                    sortedErrors.forEach { match ->
                                                                        if (match.suggestions
                                                                                        .isNotEmpty()
                                                                        ) {
                                                                            val suggestion =
                                                                                    match.suggestions
                                                                                            .first()
                                                                            // Ensure range is
                                                                            // within bounds
                                                                            // (safety check)
                                                                            if (match.range.last +
                                                                                            1 <=
                                                                                            newText.length
                                                                            ) {
                                                                                newText =
                                                                                        newText.replaceRange(
                                                                                                match.range
                                                                                                        .first,
                                                                                                match.range
                                                                                                        .last +
                                                                                                        1,
                                                                                                suggestion
                                                                                        )
                                                                                formattedTextChange =
                                                                                        true
                                                                            }
                                                                        }
                                                                    }

                                                                    if (formattedTextChange) {
                                                                        // Move cursor to end of
                                                                        // text
                                                                        messageInput =
                                                                                TextFieldValue(
                                                                                        newText,
                                                                                        TextRange(
                                                                                                newText.length
                                                                                        )
                                                                                )
                                                                    }
                                                                }
                                                            }
                                                        }
                                                        true
                                                    }
                                                    else -> false
                                                }
                                            },
                            textStyle = TextStyle(color = colors.foreground, fontSize = 14.sp),
                            cursorBrush = SolidColor(colors.cyan),
                            singleLine = false,
                            maxLines = 5
                    )

                    IconButton(
                            onClick = {
                                if (messageInput.text.isNotBlank()) {
                                    onSendMessage(messageInput.text)
                                    messageInput = TextFieldValue("")
                                }
                            },
                            modifier = Modifier.align(Alignment.CenterEnd).padding(end = 4.dp)
                    ) {
                        Icon(
                                imageVector = Icons.AutoMirrored.Filled.Send,
                                contentDescription = "Send",
                                tint = colors.green
                        )
                    }

                    DropdownMenu(
                            expanded = showSpellCheckMenu,
                            onDismissRequest = { showSpellCheckMenu = false }
                    ) {
                        spellCheckSuggestions.forEach { suggestion ->
                            DropdownMenuItem(
                                    text = { Text(suggestion, color = colors.foreground) },
                                    onClick = {
                                        // Apply suggestion
                                        // need to find range again safely
                                        val cursor = messageInput.selection.start
                                        // Find valid range from our errors list that matches cursor
                                        val match =
                                                spellCheckErrors.find {
                                                    cursor >= it.range.first &&
                                                            cursor <= (it.range.last + 1)
                                                }

                                        if (match != null) {
                                            val start = match.range.first
                                            val end = match.range.last + 1

                                            // Double check bounds against current text
                                            if (start >= 0 && end <= messageInput.text.length) {
                                                val newText =
                                                        messageInput.text.replaceRange(
                                                                start,
                                                                end,
                                                                suggestion
                                                        )
                                                // Move cursor to end of inserted word
                                                messageInput =
                                                        TextFieldValue(
                                                                newText,
                                                                TextRange(start + suggestion.length)
                                                        )
                                            }
                                        }
                                        showSpellCheckMenu = false
                                    }
                            )
                        }
                    }
                }

                Spacer(modifier = Modifier.width(8.dp))

                // Image upload button
                var showFilePicker by remember { mutableStateOf(false) }

                if (showFilePicker) {
                    LaunchedEffect(Unit) {
                        scope.launch {
                            val fileDialog =
                                    java.awt.FileDialog(
                                            java.awt.Frame(),
                                            "Select Image",
                                            java.awt.FileDialog.LOAD
                                    )
                            fileDialog.setFilenameFilter { _, name ->
                                val ext = name.lowercase()
                                ext.endsWith(".png") ||
                                        ext.endsWith(".jpg") ||
                                        ext.endsWith(".jpeg") ||
                                        ext.endsWith(".gif") ||
                                        ext.endsWith(".bmp")
                            }
                            fileDialog.isVisible = true

                            val selectedFile =
                                    if (fileDialog.file != null) {
                                        java.io.File(fileDialog.directory, fileDialog.file)
                                    } else null

                            showFilePicker = false

                            selectedFile?.let { file ->
                                if (file.exists() && file.isFile) {
                                    val uploadedUrl =
                                            connectionManager.uploadImage(
                                                    file.readBytes(),
                                                    file.name
                                            )
                                    if (uploadedUrl != null) {
                                        val newText =
                                                if (messageInput.text.isNotEmpty()) {
                                                    messageInput.text + " " + uploadedUrl
                                                } else {
                                                    uploadedUrl
                                                }
                                        messageInput =
                                                TextFieldValue(newText, TextRange(newText.length))
                                    }
                                }
                            }
                        }
                    }
                }

                Button(
                        onClick = { showFilePicker = true },
                        colors = ButtonDefaults.buttonColors(containerColor = Color(0xFF2E7D32))
                ) { Text("Image Upload", color = Color.White) }
            }
        }
    }
}

/**
 * Finds the message at a given screen position within the LazyColumn. Returns pair of (messageKey,
 * localY) where localY is relative to the item.
 */
private fun findMessageAtPosition(
        position: Offset,
        listState: androidx.compose.foundation.lazy.LazyListState,
        messages: List<ChatUiItem>
): Pair<String, Float>? {
    val visibleItems = listState.layoutInfo.visibleItemsInfo
    for (item in visibleItems) {
        val itemTop = item.offset.toFloat()
        val itemBottom = (item.offset + item.size).toFloat()
        if (position.y >= itemTop && position.y < itemBottom) {
            val messageKey = messages.getOrNull(item.index)?.id
            if (messageKey != null) {
                return messageKey to (position.y - itemTop)
            }
        }
    }
    return null
}
