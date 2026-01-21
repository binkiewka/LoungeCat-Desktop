package com.loungecat.irc.ui.screen

// import loungecat_desktop.shared.generated.resources.Res
// import loungecat_desktop.shared.generated.resources.logo_transparent
// import com.loungecat.irc.ui.components.SplitPane

import androidx.compose.animation.core.animateDpAsState
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ExitToApp
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowLeft
import androidx.compose.material.icons.automirrored.filled.KeyboardArrowRight
import androidx.compose.material.icons.filled.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.*
import androidx.compose.ui.input.pointer.isSecondaryPressed
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.DpOffset
import androidx.compose.ui.unit.dp
import androidx.compose.ui.zIndex
import com.loungecat.irc.data.model.*
import com.loungecat.irc.data.model.ChannelUser
import com.loungecat.irc.service.DesktopConnectionManager
import com.loungecat.irc.ui.components.AppTooltip
import com.loungecat.irc.ui.components.ChannelPane
import com.loungecat.irc.ui.components.ChatGrid
import com.loungecat.irc.ui.components.CustomVerticalScrollbar
import com.loungecat.irc.ui.components.ExportDialog
import com.loungecat.irc.ui.components.QuickModerationDialog
import com.loungecat.irc.ui.components.UserContextMenu
import com.loungecat.irc.ui.components.WhoisDialog
import com.loungecat.irc.ui.components.rememberSplitViewState
import com.loungecat.irc.ui.theme.AppColors
import com.loungecat.irc.util.TabCompletionHelper
import java.util.*

// private enum class ActivePane {
//    LEFT,
//    RIGHT
// }

@Composable
fun DesktopMainScreen(connectionManager: DesktopConnectionManager) {
    val colors = AppColors.current
    val scope = rememberCoroutineScope()

    val servers by connectionManager.servers.collectAsState()
    val currentServerId by connectionManager.currentServerId.collectAsState()
    val connectionState by connectionManager.connectionState.collectAsState()
    val channels by connectionManager.channels.collectAsState()
    val currentChannel by connectionManager.currentChannel.collectAsState()
    val messages by connectionManager.messages.collectAsState()
    val urlPreviews by connectionManager.urlPreviews.collectAsState()
    val imageUrls by connectionManager.imageUrls.collectAsState()
    val showJoinPartMessages by connectionManager.joinPartQuitMode.collectAsState()
    val userPreferences by connectionManager.userPreferences.collectAsState()

    var showAddServerDialog by remember { mutableStateOf(false) }
    var showJoinChannelDialog by remember { mutableStateOf(false) }
    var showSettingsDialog by remember { mutableStateOf(false) }
    var showEditServerDialog by remember { mutableStateOf(false) }
    var editingServerConfig by remember { mutableStateOf<ServerConfig?>(null) }
    var serverContextMenuState by remember { mutableStateOf<Pair<Long, Boolean>?>(null) }
    var channelContextMenuState by remember { mutableStateOf<Pair<String, Boolean>?>(null) }
    var showIgnoredUsersDialog by remember { mutableStateOf(false) }
    var messageInput by remember { mutableStateOf("") }
    val tabCompletionHelper = remember { TabCompletionHelper() }
    var selectedUserForContextMenu by remember { mutableStateOf<ChannelUser?>(null) }
    var showModerationDialog by remember { mutableStateOf(false) }
    var moderationTargetUser by remember { mutableStateOf<String?>(null) }
    var isUserListVisible by remember { mutableStateOf(true) }
    var showExportDialog by remember { mutableStateOf(false) }
    var exportChannelName by remember { mutableStateOf<String?>(null) }
    var showWhoisDialog by remember { mutableStateOf(false) }
    var whoisTargetUser by remember { mutableStateOf<String?>(null) }
    val expandedServerIds = remember { mutableStateListOf<Long>() }

    val splitViewState = rememberSplitViewState()
    // var activeSplitPane by remember { mutableStateOf(ActivePane.LEFT) }

    // Update check
    var updateAvailable by remember {
        mutableStateOf<com.loungecat.irc.data.model.UpdateResult?>(null)
    }
    val uriHandler = androidx.compose.ui.platform.LocalUriHandler.current

    LaunchedEffect(Unit) {
        val service = com.loungecat.irc.service.UpdateCheckService()
        // Check every 6 hours (initial check + periodic)
        val checkIntervalMs = 6 * 60 * 60 * 1000L // 6 hours in milliseconds
        while (true) {
            val result = service.checkForUpdates()
            if (result != null &&
                            result.hasUpdate &&
                            updateAvailable?.latestVersion != result.latestVersion
            ) {
                updateAvailable = result
            }
            kotlinx.coroutines.delay(checkIntervalMs)
        }
    }

    LaunchedEffect(userPreferences.spellCheckLanguage) {
        com.loungecat.irc.util.SpellChecker.initialize(userPreferences.spellCheckLanguage)
    }

    // Sync split view state: Remove channels that are no longer joined
    LaunchedEffect(channels) {
        if (splitViewState.isEnabled) {
            val validChannels = channels.map { it.name }.toSet()
            // We need to check across all servers, but channels is just for current server?
            // Wait, connectionManager.channels is for CURRENT server.
            // Split view can theoretically have channels from different servers if we supported it
            // fully,
            // but currently the UI focuses on one server at a time for the sidebar list.
            // However, split view MIGHT hold channels from other servers if we switch servers?
            // Actually ServerItem click switches server.

            // Let's safe-guard by checking if the channel is valid for the server it belongs to.
            // But we don't have easy access to ALL channels of ALL servers here efficiently without
            // observing all.
            //
            // Simplified approach: If the channel belongs to the CURRENT server and is NOT in
            // `channels`, remove it.
            // If it belongs to another server, we skip checking (or we'd need to observe all
            // servers).

            // Per DesktopConnectionManager structure, we have access to `connections`.
            // But strict state observation suggests we should trust the reactive flows.

            // For now, let's implement for CURRENT server channels, as that's the primary use case
            // for "parting".

            val currentSrvId = currentServerId
            if (currentSrvId != null) {
                // We must use a copy to avoid concurrent modification if we remove items
                val activeList = splitViewState.activeChannels.toList()

                activeList.forEachIndexed { index, (srvId, chName) ->
                    if (srvId == currentSrvId) {
                        if (!validChannels.contains(chName)) {
                            // Channel is no longer in the list for this server -> Close it
                            // We need to find the CURRENT index in the live list, as it might have
                            // shifted
                            val currentIndex =
                                    splitViewState.activeChannels.indexOfFirst {
                                        it.first == srvId && it.second == chName
                                    }
                            if (currentIndex != -1) {
                                splitViewState.closeChannel(currentIndex)
                            }
                        }
                    }
                }
            }
        }
    }

    // Removed currentMessages as it is now handled in ChatPanel
    val currentUsers = currentChannel?.let { connectionManager.getChannelUsers(it) } ?: emptyList()

    // LazyListState for the servers/channels sidebar
    val sidebarListState = rememberLazyListState()

    var isSidebarVisible by remember { mutableStateOf(true) }
    val sidebarWidth by animateDpAsState(targetValue = if (isSidebarVisible) 250.dp else 50.dp)

    Row(modifier = Modifier.fillMaxSize().background(colors.background)) {
        Column(
                modifier =
                        Modifier.width(sidebarWidth)
                                .fillMaxHeight()
                                .background(colors.windowBackground)
        ) {
            if (isSidebarVisible) {
                // EXPANDED SIDEBAR CONTENT
                Row(
                        modifier = Modifier.fillMaxWidth().padding(12.dp),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically
                ) {
                    Text(
                            text = "Servers",
                            style = MaterialTheme.typography.titleMedium,
                            color = colors.foreground,
                            fontWeight = FontWeight.Bold
                    )
                    Row {
                        AppTooltip(text = "Toggle Split View") {
                            IconButton(
                                    onClick = {
                                        connectionManager.setSplitViewEnabled(
                                                !userPreferences.splitViewEnabled
                                        )
                                    },
                                    modifier = Modifier.size(28.dp)
                            ) {
                                Icon(
                                        imageVector =
                                                if (userPreferences.splitViewEnabled)
                                                        Icons.Default.VerticalSplit
                                                else Icons.Default.CropPortrait,
                                        contentDescription = "Toggle Split View",
                                        tint =
                                                if (userPreferences.splitViewEnabled) colors.cyan
                                                else colors.comment,
                                        modifier = Modifier.size(18.dp)
                                )
                            }
                        }
                        AppTooltip(text = "Settings") {
                            IconButton(
                                    onClick = { showSettingsDialog = true },
                                    modifier = Modifier.size(28.dp)
                            ) {
                                Icon(
                                        imageVector = Icons.Default.Settings,
                                        contentDescription = "Settings",
                                        tint = colors.comment,
                                        modifier = Modifier.size(18.dp)
                                )
                            }
                        }

                        AppTooltip(text = "Collapse Sidebar") {
                            IconButton(
                                    onClick = { isSidebarVisible = false },
                                    modifier = Modifier.size(28.dp)
                            ) {
                                Icon(
                                        imageVector = Icons.AutoMirrored.Filled.KeyboardArrowLeft,
                                        contentDescription = "Collapse Sidebar",
                                        tint = colors.comment,
                                        modifier = Modifier.size(18.dp)
                                )
                            }
                        }
                        // Moved "Add Server" to end or maybe just keep it separate?
                        // Let's keep it here but compact.
                        AppTooltip(text = "Add Server") {
                            IconButton(
                                    onClick = { showAddServerDialog = true },
                                    modifier = Modifier.size(28.dp)
                            ) {
                                Icon(
                                        imageVector = Icons.Default.Add,
                                        contentDescription = "Add Server",
                                        tint = colors.green,
                                        modifier = Modifier.size(18.dp)
                                )
                            }
                        }
                    }
                }

                HorizontalDivider(color = colors.border, thickness = 1.dp)

                Box(modifier = Modifier.weight(1f)) {
                    LazyColumn(state = sidebarListState, modifier = Modifier.fillMaxSize()) {
                        if (servers.isEmpty()) {
                            item {
                                Text(
                                        text = "No servers configured.\nClick + to add a server.",
                                        color = colors.comment,
                                        modifier = Modifier.padding(16.dp),
                                        style = MaterialTheme.typography.bodyMedium
                                )
                            }
                        }

                        items(servers) { server ->
                            ServerItem(
                                    server = server,
                                    isSelected = server.serverId == currentServerId,
                                    currentChannel = currentChannel,
                                    showContextMenu =
                                            serverContextMenuState?.first == server.serverId &&
                                                    serverContextMenuState?.second == true,
                                    channelContextMenuState = channelContextMenuState,
                                    isExpanded = expandedServerIds.contains(server.serverId),
                                    onServerClick = {
                                        if (expandedServerIds.contains(server.serverId)) {
                                            expandedServerIds.remove(server.serverId)
                                        } else {
                                            expandedServerIds.add(server.serverId)
                                        }
                                    },
                                    onServerRightClick = {
                                        serverContextMenuState = Pair(server.serverId, true)
                                    },
                                    onDismissContextMenu = { serverContextMenuState = null },
                                    onChannelClick = { channelName ->
                                        connectionManager.switchToServer(server.serverId)
                                        connectionManager.setCurrentChannel(channelName)

                                        if (userPreferences.splitViewEnabled) {
                                            if (!splitViewState.isEnabled) {
                                                // Transition from Single -> Grid (Current + New)
                                                val currentSrv = currentServerId
                                                val currentCh = currentChannel
                                                if (currentSrv != null && currentCh != null) {
                                                    splitViewState.enable(
                                                            Pair(currentSrv, currentCh)
                                                    )
                                                    splitViewState.addChannel(
                                                            server.serverId,
                                                            channelName
                                                    )
                                                } else {
                                                    splitViewState.enable(
                                                            Pair(server.serverId, channelName)
                                                    )
                                                }
                                            } else {
                                                // Already in Grid Mode -> Add or Replace Active
                                                splitViewState.addChannel(
                                                        server.serverId,
                                                        channelName
                                                )
                                            }
                                        }
                                    },
                                    onChannelClose = { channelName ->
                                        connectionManager.partChannel(channelName)
                                    },
                                    onChannelRightClick = { channelName ->
                                        channelContextMenuState = Pair(channelName, true)
                                    },
                                    onDismissChannelContextMenu = {
                                        channelContextMenuState = null
                                    },
                                    onListIgnoredUsers = { showIgnoredUsersDialog = true },
                                    onClearChannelHistory = { channelName ->
                                        connectionManager.clearChannelHistory(
                                                server.serverId,
                                                channelName
                                        )
                                    },
                                    onLeaveChannel = { channelName ->
                                        connectionManager.partChannel(server.serverId, channelName)
                                    },
                                    onJoinNewChannel = {
                                        connectionManager.switchToServer(server.serverId)
                                        showJoinChannelDialog = true
                                    },
                                    onConnect = {
                                        connectionManager.connectServer(server.serverId)
                                    },
                                    onDisconnect = {
                                        connectionManager.disconnectServer(server.serverId)
                                    },
                                    onEdit = {
                                        val connection =
                                                connectionManager.getConnection(server.serverId)
                                        editingServerConfig = connection?.config
                                        showEditServerDialog = true
                                        serverContextMenuState = null
                                    },
                                    onRemove = {
                                        connectionManager.removeServer(server.serverId)
                                        serverContextMenuState = null
                                    },
                                    onExportChannel = { channelName ->
                                        exportChannelName = channelName
                                        showExportDialog = true
                                    },
                                    onOpenSplitView = { channelName ->
                                        // Auto-enable split view if disabled
                                        if (!userPreferences.splitViewEnabled) {
                                            connectionManager.setSplitViewEnabled(true)
                                        }

                                        if (splitViewState.isEnabled) {
                                            splitViewState.addChannel(server.serverId, channelName)
                                        } else {
                                            // Enable split view logic
                                            val currentSrv = currentServerId
                                            val currentCh = currentChannel
                                            if (currentSrv != null && currentCh != null) {
                                                // If we are opening a NEW channel in split view,
                                                // maybe
                                                // keep
                                                // current as #1 and new as #2?
                                                splitViewState.enable(Pair(currentSrv, currentCh))
                                                splitViewState.addChannel(
                                                        server.serverId,
                                                        channelName
                                                )
                                            } else {
                                                splitViewState.enable(
                                                        Pair(server.serverId, channelName)
                                                )
                                            }
                                        }
                                        // Switch to ensure selection update if needed
                                        connectionManager.switchToServer(server.serverId)
                                        connectionManager.setCurrentChannel(channelName)
                                    }
                            )
                        }
                    }
                    CustomVerticalScrollbar(
                            listState = sidebarListState,
                            modifier = Modifier.align(Alignment.CenterEnd)
                    )
                }

                // Update Button at Bottom of Sidebar
                if (updateAvailable?.hasUpdate == true) {
                    HorizontalDivider(color = colors.border, thickness = 1.dp)
                    Box(modifier = Modifier.fillMaxWidth().padding(8.dp)) {
                        Button(
                                onClick = {
                                    updateAvailable?.releaseUrl?.let { url ->
                                        uriHandler.openUri(url)
                                    }
                                },
                                colors =
                                        ButtonDefaults.buttonColors(
                                                containerColor = colors.orange.copy(alpha = 0.8f),
                                                contentColor = Color.White
                                        ),
                                modifier = Modifier.fillMaxWidth().height(40.dp),
                                shape = RoundedCornerShape(4.dp),
                                contentPadding = PaddingValues(horizontal = 8.dp)
                        ) {
                            Row(
                                    verticalAlignment = Alignment.CenterVertically,
                                    horizontalArrangement = Arrangement.Center
                            ) {
                                Icon(
                                        imageVector = Icons.Default.SystemUpdate,
                                        contentDescription = null,
                                        modifier = Modifier.size(16.dp)
                                )
                                Spacer(modifier = Modifier.width(8.dp))
                                Text(
                                        text = "Update Available",
                                        style = MaterialTheme.typography.labelLarge,
                                        fontWeight = FontWeight.Bold
                                )
                            }
                        }
                    }
                }
            } else {
                // COLLAPSED SIDEBAR CONTENT (Rail)
                Column(
                        modifier = Modifier.fillMaxSize().padding(vertical = 8.dp),
                        horizontalAlignment = Alignment.CenterHorizontally,
                        verticalArrangement = Arrangement.Top
                ) {
                    AppTooltip(text = "Expand Sidebar") {
                        IconButton(
                                onClick = { isSidebarVisible = true },
                                modifier = Modifier.size(32.dp)
                        ) {
                            Icon(
                                    imageVector = Icons.AutoMirrored.Filled.KeyboardArrowRight,
                                    contentDescription = "Expand Sidebar",
                                    tint = colors.cyan,
                                    modifier = Modifier.size(24.dp)
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(16.dp))

                    // Optional: Show server icons/initials here?
                    // For now, minimal implementation as requested "expand/hide"
                }
            }
        }

        // VerticalDivider(modifier = Modifier.fillMaxHeight().width(1.dp), color = colors.border)

        Column(modifier = Modifier.weight(1f).fillMaxHeight()) {
            if (userPreferences.splitViewEnabled && splitViewState.isEnabled) {
                // SPLIT VIEW MODE
                ChatGrid(
                        splitViewState = splitViewState,
                        connectionManager = connectionManager,
                        userPreferences = userPreferences,
                        onSendMessage = { serverId, channelName, message ->
                            connectionManager.sendMessage(serverId, channelName, message)
                        },
                        onJoinChannel = { serverId ->
                            connectionManager.switchToServer(serverId)
                            showJoinChannelDialog = true
                        },
                        onUserClick = { user -> selectedUserForContextMenu = user }
                )
            } else {
                // SINGLE VIEW MODE
                val currentSrv = currentServerId
                val currentCh = currentChannel

                if (currentSrv != null && currentCh != null) {
                    ChannelPane(
                            serverId = currentSrv,
                            channelName = currentCh,
                            connectionManager = connectionManager,
                            userPreferences = userPreferences,
                            onSendMessage = { msg ->
                                connectionManager.sendMessage(currentSrv, currentCh, msg)
                            },
                            onJoinChannel = { showJoinChannelDialog = true },
                            onUserClick = { user -> selectedUserForContextMenu = user }
                    )
                } else {
                    // Welcome / No Channel Selected State
                    Box(
                            modifier =
                                    Modifier.fillMaxSize().background(colors.background).zIndex(1f),
                            contentAlignment = Alignment.Center
                    ) {
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            Text(
                                    text = "Welcome to LoungeCat",
                                    style = MaterialTheme.typography.headlineMedium,
                                    color = colors.foreground
                            )
                            Spacer(modifier = Modifier.height(8.dp))
                            Text(
                                    text =
                                            when {
                                                servers.isEmpty() -> "Add a server to get started"
                                                else -> "Select a channel to start chatting"
                                            },
                                    style = MaterialTheme.typography.bodyLarge,
                                    color = colors.comment
                            )
                        }
                    }
                }
            }
        }
    }

    if (showAddServerDialog) {
        AddServerDialog(
                onDismiss = { showAddServerDialog = false },
                onConnect = { config ->
                    connectionManager.connect(config, isNew = true)
                    showAddServerDialog = false
                }
        )
    }

    if (showJoinChannelDialog) {
        JoinChannelDialog(
                onDismiss = { showJoinChannelDialog = false },
                onJoin = { channelName ->
                    connectionManager.joinChannel(channelName)
                    showJoinChannelDialog = false
                }
        )
    }

    selectedUserForContextMenu?.let { user ->
        val isIgnored = userPreferences.ignoredUsers.contains(user.nickname.lowercase())
        val isTrusted = userPreferences.trustedPreviewUsers.contains(user.nickname.lowercase())

        UserContextMenu(
                user = user,
                onDismiss = { selectedUserForContextMenu = null },
                onStartPM = { nickname ->
                    connectionManager.startPrivateMessage(nickname)
                    selectedUserForContextMenu = null
                },
                onWhois = { nickname ->
                    if (currentServerId != null) {
                        connectionManager.requestSilentWhois(currentServerId!!, nickname)
                        whoisTargetUser = nickname
                        showWhoisDialog = true
                    }
                    selectedUserForContextMenu = null
                },
                onModerate = { nickname ->
                    moderationTargetUser = nickname
                    showModerationDialog = true
                    selectedUserForContextMenu = null
                },
                onToggleIgnore = { nickname ->
                    if (isIgnored) {
                        connectionManager.removeIgnoredUser(nickname)
                    } else {
                        connectionManager.ignoreUser(nickname)
                    }
                    selectedUserForContextMenu = null
                },
                isIgnored = isIgnored,
                onToggleTrust = { nickname ->
                    if (isTrusted) {
                        connectionManager.untrustUserForPreviews(nickname)
                    } else {
                        connectionManager.trustUserForPreviews(nickname)
                    }
                    selectedUserForContextMenu = null
                },
                isTrusted = isTrusted
        )
    }

    if (showModerationDialog && moderationTargetUser != null && currentChannel != null) {
        QuickModerationDialog(
                targetNickname = moderationTargetUser!!,
                channelName = currentChannel!!,
                onDismiss = {
                    showModerationDialog = false
                    moderationTargetUser = null
                },
                onAction = { action ->
                    connectionManager.sendMessage(action)
                    showModerationDialog = false
                    moderationTargetUser = null
                }
        )
    }

    if (showSettingsDialog) {
        SettingsDialog(
                userPreferences = userPreferences,
                onFontSizeChange = { connectionManager.setFontSize(it) },
                onTimestampFormatChange = { connectionManager.setTimestampFormat(it) },
                onUrlImageDisplayModeChange = { connectionManager.setUrlImageDisplayMode(it) },
                onColoredNicknamesChange = { connectionManager.setColoredNicknames(it) },
                onJoinPartQuitModeChange = { connectionManager.setJoinPartQuitMode(it) },
                onSmartHideMinutesChange = { connectionManager.setSmartHideMinutes(it) },
                onSplitViewEnabledChange = { connectionManager.setSplitViewEnabled(it) },
                onSpellCheckEnabledChange = { connectionManager.setSpellCheckEnabled(it) },
                onSpellCheckLanguageChange = { connectionManager.setSpellCheckLanguage(it) },
                onThemeChange = { connectionManager.setTheme(it) },
                onOpenThemesFolder = { com.loungecat.irc.ui.theme.ThemeManager.openThemesFolder() },
                onSoundAlertsEnabledChange = { connectionManager.setSoundAlertsEnabled(it) },
                onSoundVolumeChange = { connectionManager.setSoundVolume(it) },
                onSoundOnMentionChange = { connectionManager.setSoundOnMention(it) },
                onSoundOnPrivateMessageChange = { connectionManager.setSoundOnPrivateMessage(it) },
                onSoundOnHighlightChange = { /* TODO: Implement highlighting sound toggle if needed */
                },
                onLoggingEnabledChange = { connectionManager.setLoggingEnabled(it) },
                onHistoryReplayLinesChange = { connectionManager.setHistoryReplayLines(it) },
                onProcessLinkPreviewsFromOthersChange = {
                    connectionManager.setProcessLinkPreviewsFromOthers(it)
                },
                onUseProxyForPreviewsChange = { connectionManager.setUseProxyForPreviews(it) },
                onPastebinThresholdChange = { connectionManager.setPastebinThreshold(it) },
                onImgbbApiKeyChange = { connectionManager.setImgbbApiKey(it) },
                onUrlShorteningEnabledChange = { connectionManager.setUrlShorteningEnabled(it) },
                onUrlShorteningThresholdChange = {
                    connectionManager.setUrlShorteningThreshold(it)
                },
                onDismiss = { showSettingsDialog = false }
        )
    }

    if (showEditServerDialog && editingServerConfig != null) {
        EditServerDialog(
                config = editingServerConfig!!,
                onDismiss = {
                    showEditServerDialog = false
                    editingServerConfig = null
                },
                onSave = { updatedConfig, reconnect ->
                    if (reconnect) {
                        connectionManager.connect(updatedConfig)
                    } else {
                        connectionManager.updateServerConfig(updatedConfig)
                    }
                    showEditServerDialog = false
                    editingServerConfig = null
                }
        )
    }

    if (showIgnoredUsersDialog) {
        IgnoredUsersDialog(
                ignoredUsers = userPreferences.ignoredUsers,
                onDismiss = { showIgnoredUsersDialog = false },
                onUnignore = { user -> connectionManager.removeIgnoredUser(user) }
        )
    }

    if (showExportDialog && exportChannelName != null) {
        val exportMessages = messages[exportChannelName] ?: emptyList()
        ExportDialog(
                channelName = exportChannelName!!,
                messages = exportMessages,
                onDismiss = {
                    showExportDialog = false
                    exportChannelName = null
                }
        )
    }

    if (showWhoisDialog && whoisTargetUser != null) {
        val whoisDataMap by connectionManager.whoisCache.collectAsState()
        val nickname = whoisTargetUser!!
        val data = whoisDataMap[nickname.lowercase()]

        WhoisDialog(
                nickname = nickname,
                whoisData = data,
                onDismiss = {
                    showWhoisDialog = false
                    whoisTargetUser = null
                }
        )
    }

    Box(modifier = Modifier.fillMaxSize(), contentAlignment = Alignment.BottomCenter) {
        // Obsolete SnackbarHost removed from here
    }
}

@Composable
private fun ChannelListItem(
        channel: Channel,
        isSelected: Boolean,
        onClick: () -> Unit,
        onClose: (() -> Unit)? = null,
        onRightClick: () -> Unit = {},
        showContextMenu: Boolean = false,
        onDismissContextMenu: () -> Unit = {},
        onListIgnoredUsers: () -> Unit = {},
        onClearHistory: () -> Unit = {},
        onLeaveChannel: () -> Unit = {},
        onExport: () -> Unit = {},
        onOpenSplitView: () -> Unit = {}
) {
    val colors = AppColors.current

    Box {
        Row(
                modifier =
                        Modifier.fillMaxWidth()
                                .clickable(onClick = onClick)
                                .pointerInput(Unit) {
                                    awaitPointerEventScope {
                                        while (true) {
                                            val event = awaitPointerEvent()
                                            if (event.buttons.isSecondaryPressed) {
                                                onRightClick()
                                            }
                                        }
                                    }
                                }
                                .background(
                                        if (isSelected) colors.highlightBg else Color.Transparent
                                )
                                .padding(start = 36.dp, end = 8.dp, top = 4.dp, bottom = 4.dp),
                verticalAlignment = Alignment.CenterVertically
        ) {
            Icon(
                    imageVector =
                            when (channel.type) {
                                ChannelType.CHANNEL -> Icons.Default.Tag
                                ChannelType.QUERY -> Icons.Default.Person
                                ChannelType.SERVER -> Icons.Default.Dns
                            },
                    contentDescription = null,
                    tint =
                            when {
                                isSelected -> colors.cyan
                                channel.type == ChannelType.QUERY -> colors.cyan
                                else -> colors.comment
                            },
                    modifier = Modifier.size(14.dp)
            )
            Spacer(modifier = Modifier.width(6.dp))
            Text(
                    text = channel.displayName ?: channel.name,
                    color = if (isSelected) colors.cyan else colors.foreground,
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.weight(1f)
            )
            // Activity indicator (green dot for recent activity)
            if (channel.hasRecentActivity && !isSelected) {
                Box(
                        modifier =
                                Modifier.size(6.dp)
                                        .background(
                                                colors.green.copy(alpha = 0.7f),
                                                RoundedCornerShape(50)
                                        )
                )
                Spacer(modifier = Modifier.width(4.dp))
            }
            if (channel.unreadCount > 0) {
                Surface(shape = RoundedCornerShape(10.dp), color = colors.pink) {
                    Text(
                            text =
                                    if (channel.unreadCount > 99) "99+"
                                    else channel.unreadCount.toString(),
                            style = MaterialTheme.typography.labelSmall,
                            color = colors.windowBackground,
                            modifier = Modifier.padding(horizontal = 6.dp, vertical = 2.dp)
                    )
                }
                Spacer(modifier = Modifier.width(4.dp))
            }
            if (onClose != null && channel.type != ChannelType.SERVER) {
                AppTooltip(text = "Leave Channel") {
                    IconButton(onClick = onClose, modifier = Modifier.size(18.dp)) {
                        Icon(
                                imageVector = Icons.Default.Close,
                                contentDescription = "Leave Channel",
                                tint = colors.comment,
                                modifier = Modifier.size(12.dp)
                        )
                    }
                }
            }
        }

        DropdownMenu(
                expanded = showContextMenu,
                onDismissRequest = onDismissContextMenu,
                offset = DpOffset(80.dp, 0.dp),
                containerColor = colors.windowBackground
        ) {
            DropdownMenuItem(
                    text = { Text("List Ignored Users", color = colors.foreground) },
                    leadingIcon = { Icon(Icons.Default.PersonOff, null, tint = colors.orange) },
                    onClick = {
                        onListIgnoredUsers()
                        onDismissContextMenu()
                    }
            )
            DropdownMenuItem(
                    text = { Text("Open in Split View", color = colors.foreground) },
                    leadingIcon = { Icon(Icons.Default.VerticalSplit, null, tint = colors.cyan) },
                    onClick = {
                        onOpenSplitView()
                        onDismissContextMenu()
                    }
            )
            DropdownMenuItem(
                    text = { Text("Export Chat Log", color = colors.foreground) },
                    leadingIcon = { Icon(Icons.Default.FileDownload, null, tint = colors.cyan) },
                    onClick = {
                        onExport()
                        onDismissContextMenu()
                    }
            )
            DropdownMenuItem(
                    text = { Text("Clear Channel", color = colors.foreground) },
                    leadingIcon = { Icon(Icons.Default.DeleteSweep, null, tint = colors.yellow) },
                    onClick = {
                        onClearHistory()
                        onDismissContextMenu()
                    }
            )
            if (channel.type != ChannelType.SERVER) {
                DropdownMenuItem(
                        text = { Text("Leave Channel", color = colors.red) },
                        leadingIcon = {
                            Icon(Icons.AutoMirrored.Filled.ExitToApp, null, tint = colors.red)
                        },
                        onClick = {
                            onLeaveChannel()
                            onDismissContextMenu()
                        }
                )
            }
        }
    }
}

@Composable
private fun ServerItem(
        server: com.loungecat.irc.service.ServerListItem,
        isSelected: Boolean,
        isExpanded: Boolean,
        currentChannel: String?,
        showContextMenu: Boolean,
        channelContextMenuState: Pair<String, Boolean>?,
        onServerClick: () -> Unit,
        onServerRightClick: () -> Unit,
        onDismissContextMenu: () -> Unit,
        onChannelClick: (String) -> Unit,
        onChannelClose: (String) -> Unit,
        onChannelRightClick: (String) -> Unit,
        onDismissChannelContextMenu: () -> Unit,
        onListIgnoredUsers: () -> Unit,
        onClearChannelHistory: (String) -> Unit,
        onLeaveChannel: (String) -> Unit,
        onJoinNewChannel: () -> Unit,
        onConnect: () -> Unit,
        onDisconnect: () -> Unit,
        onEdit: () -> Unit,
        onRemove: () -> Unit,
        onExportChannel: (String) -> Unit = {},
        onOpenSplitView: (String) -> Unit = {}
) {
    val colors = AppColors.current

    Column {
        Box {
            Row(
                    modifier =
                            Modifier.fillMaxWidth()
                                    .clickable(onClick = onServerClick)
                                    .pointerInput(Unit) {
                                        awaitPointerEventScope {
                                            while (true) {
                                                val event = awaitPointerEvent()
                                                if (event.buttons.isSecondaryPressed) {
                                                    onServerRightClick()
                                                }
                                            }
                                        }
                                    }
                                    .background(
                                            if (isSelected) colors.selection else Color.Transparent
                                    )
                                    .padding(horizontal = 12.dp, vertical = 8.dp),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                            imageVector =
                                    if (isExpanded) Icons.Default.KeyboardArrowDown
                                    else Icons.AutoMirrored.Filled.KeyboardArrowRight,
                            contentDescription = if (isExpanded) "Collapse" else "Expand",
                            tint = colors.comment,
                            modifier = Modifier.size(16.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Icon(
                            imageVector =
                                    if (server.isConnected) Icons.Default.Cloud
                                    else Icons.Default.CloudOff,
                            contentDescription = null,
                            tint = if (server.isConnected) colors.green else colors.comment,
                            modifier = Modifier.size(16.dp)
                    )
                    Spacer(modifier = Modifier.width(8.dp))
                    Text(
                            text = server.serverName.ifEmpty { server.hostname },
                            color = colors.foreground,
                            style = MaterialTheme.typography.bodyMedium
                    )
                }

                Row {
                    if (server.isConnected) {
                        AppTooltip(text = "Disconnect") {
                            IconButton(onClick = onDisconnect, modifier = Modifier.size(24.dp)) {
                                Icon(
                                        imageVector = Icons.Default.PowerSettingsNew,
                                        contentDescription = "Disconnect",
                                        tint = colors.red,
                                        modifier = Modifier.size(16.dp)
                                )
                            }
                        }
                    } else {
                        AppTooltip(text = "Connect") {
                            IconButton(onClick = onConnect, modifier = Modifier.size(24.dp)) {
                                Icon(
                                        imageVector = Icons.Default.PlayArrow,
                                        contentDescription = "Connect",
                                        tint = colors.green,
                                        modifier = Modifier.size(16.dp)
                                )
                            }
                        }
                    }
                }
            }

            DropdownMenu(
                    expanded = showContextMenu,
                    onDismissRequest = onDismissContextMenu,
                    offset = DpOffset(100.dp, 0.dp),
                    containerColor = colors.windowBackground
            ) {
                DropdownMenuItem(
                        text = { Text("Join New Channel...", color = colors.foreground) },
                        leadingIcon = { Icon(Icons.Default.Add, null, tint = colors.green) },
                        onClick = {
                            onDismissContextMenu()
                            onJoinNewChannel()
                        }
                )
                DropdownMenuItem(
                        text = { Text("Edit Server", color = colors.foreground) },
                        leadingIcon = { Icon(Icons.Default.Edit, null, tint = colors.cyan) },
                        onClick = {
                            onDismissContextMenu()
                            onEdit()
                        }
                )
                DropdownMenuItem(
                        text = { Text("Remove Server", color = colors.red) },
                        leadingIcon = { Icon(Icons.Default.Delete, null, tint = colors.red) },
                        onClick = {
                            onDismissContextMenu()
                            onRemove()
                        }
                )
            }
        }

        if (isExpanded && server.isConnected) {
            val serverChannels = server.channels.filter { it.type == ChannelType.SERVER }
            val regularChannels = server.channels.filter { it.type == ChannelType.CHANNEL }
            val privateMessages = server.channels.filter { it.type == ChannelType.QUERY }

            serverChannels.forEach { channel ->
                ChannelListItem(
                        channel = channel,
                        isSelected = channel.name == currentChannel,
                        onClick = { onChannelClick(channel.name) },
                        onClose = null,
                        onRightClick = { onChannelRightClick(channel.name) },
                        showContextMenu =
                                channelContextMenuState?.first == channel.name &&
                                        channelContextMenuState?.second == true,
                        onDismissContextMenu = onDismissChannelContextMenu,
                        onListIgnoredUsers = onListIgnoredUsers,
                        onClearHistory = { onClearChannelHistory(channel.name) },
                        onLeaveChannel = { onLeaveChannel(channel.name) },
                        onExport = { onExportChannel(channel.name) },
                        onOpenSplitView = { onOpenSplitView(channel.name) }
                )
            }

            if (regularChannels.isNotEmpty()) {
                Text(
                        text = "CHANNELS",
                        style = MaterialTheme.typography.labelSmall,
                        color = colors.comment,
                        modifier = Modifier.padding(start = 36.dp, top = 8.dp, bottom = 4.dp)
                )
                regularChannels.forEach { channel ->
                    ChannelListItem(
                            channel = channel,
                            isSelected = channel.name == currentChannel,
                            onClick = { onChannelClick(channel.name) },
                            onClose = { onChannelClose(channel.name) },
                            onRightClick = { onChannelRightClick(channel.name) },
                            showContextMenu =
                                    channelContextMenuState?.first == channel.name &&
                                            channelContextMenuState?.second == true,
                            onDismissContextMenu = onDismissChannelContextMenu,
                            onListIgnoredUsers = onListIgnoredUsers,
                            onClearHistory = { onClearChannelHistory(channel.name) },
                            onLeaveChannel = { onLeaveChannel(channel.name) },
                            onExport = { onExportChannel(channel.name) },
                            onOpenSplitView = { onOpenSplitView(channel.name) }
                    )
                }
            }

            if (privateMessages.isNotEmpty()) {
                Text(
                        text = "PRIVATE MESSAGES",
                        style = MaterialTheme.typography.labelSmall,
                        color = colors.cyan,
                        modifier = Modifier.padding(start = 36.dp, top = 8.dp, bottom = 4.dp)
                )
                privateMessages.forEach { channel ->
                    ChannelListItem(
                            channel = channel,
                            isSelected = channel.name == currentChannel,
                            onClick = { onChannelClick(channel.name) },
                            onClose = { onChannelClose(channel.name) },
                            onRightClick = { onChannelRightClick(channel.name) },
                            showContextMenu =
                                    channelContextMenuState?.first == channel.name &&
                                            channelContextMenuState?.second == true,
                            onDismissContextMenu = onDismissChannelContextMenu,
                            onListIgnoredUsers = onListIgnoredUsers,
                            onClearHistory = { onClearChannelHistory(channel.name) },
                            onLeaveChannel = { onLeaveChannel(channel.name) },
                            onExport = { onExportChannel(channel.name) },
                            onOpenSplitView = { onOpenSplitView(channel.name) }
                    )
                }
            }
        }
    }
}

@Composable
private fun UserItem(user: ChannelUser, onClick: () -> Unit) {
    val colors = AppColors.current

    Row(
            modifier =
                    Modifier.fillMaxWidth()
                            .clickable(onClick = onClick)
                            .padding(horizontal = 12.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically
    ) {
        val prefix =
                when {
                    user.modes.contains(UserMode.OWNER) -> "~"
                    user.modes.contains(UserMode.ADMIN) -> "&"
                    user.modes.contains(UserMode.OP) -> "@"
                    user.modes.contains(UserMode.HALFOP) -> "%"
                    user.modes.contains(UserMode.VOICE) -> "+"
                    else -> " "
                }

        val prefixColor =
                when {
                    user.isOp -> colors.red
                    user.isVoiced -> colors.green
                    else -> colors.comment
                }

        Text(
                text = prefix,
                color = prefixColor,
                style = MaterialTheme.typography.bodySmall,
                fontWeight = FontWeight.Bold,
                modifier = Modifier.width(12.dp)
        )
        Text(
                text = user.nickname,
                color = colors.foreground,
                style = MaterialTheme.typography.bodySmall
        )
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun AddServerDialog(onDismiss: () -> Unit, onConnect: (ServerConfig) -> Unit) {
    val colors = AppColors.current

    var serverType by remember { mutableStateOf(ServerType.IRC) }
    var matrixHomeserver by remember { mutableStateOf("") }
    var matrixAccessToken by remember { mutableStateOf("") }
    var serverName by remember { mutableStateOf("") }
    var hostname by remember { mutableStateOf("") }
    var port by remember { mutableStateOf("6697") }
    var useSsl by remember { mutableStateOf(true) }
    var acceptSelfSignedCerts by remember { mutableStateOf(false) }
    var serverPassword by remember { mutableStateOf("") }
    var nickname by remember { mutableStateOf("") }
    var altNickname by remember { mutableStateOf("") }
    var username by remember { mutableStateOf("") }
    var realName by remember { mutableStateOf("") }
    var useSasl by remember { mutableStateOf(false) }
    var saslUsername by remember { mutableStateOf("") }
    var saslPassword by remember { mutableStateOf("") }
    var nickServPassword by remember { mutableStateOf("") }
    var autoJoinChannels by remember { mutableStateOf("") }
    var onConnectCommands by remember { mutableStateOf("") }
    var proxyType by remember { mutableStateOf(ProxyType.NONE) }
    var proxyHost by remember { mutableStateOf("") }
    var proxyPort by remember { mutableStateOf("1080") }
    var proxyUsername by remember { mutableStateOf("") }
    var proxyPassword by remember { mutableStateOf("") }

    AlertDialog(
            onDismissRequest = onDismiss,
            containerColor = colors.windowBackground,
            title = { Text("Add Server", color = colors.foreground) },
            text = {
                val scope = rememberCoroutineScope()
                val scrollState = rememberScrollState()
                Box(modifier = Modifier.width(420.dp).heightIn(max = 500.dp)) {
                    Column(
                            modifier =
                                    Modifier.fillMaxWidth()
                                            .verticalScroll(scrollState)
                                            .padding(end = 12.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        OutlinedTextField(
                                value = serverName,
                                onValueChange = { serverName = it },
                                label = { Text("Server Name") },
                                placeholder = {
                                    Text(
                                            if (serverType == ServerType.MATRIX) "e.g., Matrix"
                                            else "e.g., Libera Chat"
                                    )
                                },
                                modifier = Modifier.fillMaxWidth(),
                                singleLine = true
                        )

                        // Server Type Dropdown
                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("Server Type", color = colors.foreground)
                            Box {
                                var expanded by remember { mutableStateOf(false) }
                                TextButton(onClick = { expanded = true }) {
                                    Text(serverType.name, color = colors.cyan)
                                    Icon(Icons.Default.ArrowDropDown, null, tint = colors.cyan)
                                }
                                DropdownMenu(
                                        expanded = expanded,
                                        onDismissRequest = { expanded = false },
                                        containerColor = colors.windowBackground
                                ) {
                                    ServerType.entries.forEach { type ->
                                        DropdownMenuItem(
                                                text = {
                                                    Text(type.name, color = colors.foreground)
                                                },
                                                onClick = {
                                                    serverType = type
                                                    expanded = false
                                                }
                                        )
                                    }
                                }
                            }
                        }

                        if (serverType == ServerType.MATRIX) {
                            OutlinedTextField(
                                    value = matrixHomeserver,
                                    onValueChange = { matrixHomeserver = it },
                                    label = { Text("Homeserver URL") },
                                    placeholder = { Text("matrix.org") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    supportingText = {
                                        Text(
                                                "e.g., matrix.org (https:// is added automatically)",
                                                style = MaterialTheme.typography.bodySmall,
                                                color = colors.comment
                                        )
                                    }
                            )

                            OutlinedTextField(
                                    value = username,
                                    onValueChange = { username = it },
                                    label = { Text("Username (Matrix User ID)") },
                                    placeholder = { Text("@yourname:matrix.org") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    supportingText = {
                                        Text(
                                                "Your full Matrix ID including the server",
                                                style = MaterialTheme.typography.bodySmall,
                                                color = colors.comment
                                        )
                                    }
                            )

                            OutlinedTextField(
                                    value = matrixAccessToken,
                                    onValueChange = { matrixAccessToken = it },
                                    label = { Text("Password") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    visualTransformation = PasswordVisualTransformation(),
                                    supportingText = {
                                        Text(
                                                "If you use Google Login, please set a password in Element (Settings → General → Password)",
                                                style = MaterialTheme.typography.bodySmall,
                                                color = colors.comment
                                        )
                                    }
                            )

                            OutlinedTextField(
                                    value = nickname,
                                    onValueChange = { nickname = it },
                                    label = { Text("Display Name (Optional)") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = autoJoinChannels,
                                    onValueChange = { autoJoinChannels = it },
                                    label = { Text("Auto-join Rooms") },
                                    placeholder = { Text("#room:matrix.org") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    supportingText = {
                                        Text(
                                                "Room aliases to join after connecting",
                                                style = MaterialTheme.typography.bodySmall,
                                                color = colors.comment
                                        )
                                    }
                            )
                        } else {
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                OutlinedTextField(
                                        value = hostname,
                                        onValueChange = { hostname = it },
                                        label = { Text("Hostname") },
                                        placeholder = { Text("irc.libera.chat") },
                                        modifier = Modifier.weight(1f),
                                        singleLine = true
                                )
                                OutlinedTextField(
                                        value = port,
                                        onValueChange = { port = it.filter { c -> c.isDigit() } },
                                        label = { Text("Port") },
                                        modifier = Modifier.width(100.dp),
                                        singleLine = true
                                )
                            }

                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Checkbox(checked = useSsl, onCheckedChange = { useSsl = it })
                                Text("Use SSL/TLS", color = colors.foreground)
                            }

                            if (useSsl) {
                                Row(
                                        modifier = Modifier.fillMaxWidth().padding(start = 32.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.SpaceBetween
                                ) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text(
                                                "Accept Self-Signed Certificates",
                                                color = colors.foreground
                                        )
                                        Text(
                                                "Less secure - only for trusted servers",
                                                style = MaterialTheme.typography.bodySmall,
                                                color = colors.orange.copy(alpha = 0.7f)
                                        )
                                    }
                                    Checkbox(
                                            checked = acceptSelfSignedCerts,
                                            onCheckedChange = { acceptSelfSignedCerts = it }
                                    )
                                }
                            }

                            OutlinedTextField(
                                    value = serverPassword,
                                    onValueChange = { serverPassword = it },
                                    label = { Text("Server Password (optional)") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    visualTransformation = PasswordVisualTransformation()
                            )

                            HorizontalDivider(color = colors.border)

                            OutlinedTextField(
                                    value = nickname,
                                    onValueChange = { nickname = it },
                                    label = { Text("Nickname") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = altNickname,
                                    onValueChange = { altNickname = it },
                                    label = { Text("Alternative Nickname") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = username,
                                    onValueChange = { username = it },
                                    label = { Text("Username") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = realName,
                                    onValueChange = { realName = it },
                                    label = { Text("Real Name") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = nickServPassword,
                                    onValueChange = { nickServPassword = it },
                                    label = { Text("NickServ Password (optional)") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    visualTransformation = PasswordVisualTransformation(),
                                    supportingText = {
                                        Text(
                                                "Auto-identify with NickServ on connect",
                                                style = MaterialTheme.typography.bodySmall,
                                                color = colors.comment
                                        )
                                    }
                            )

                            HorizontalDivider(color = colors.border)

                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Checkbox(checked = useSasl, onCheckedChange = { useSasl = it })
                                Text("Use SASL Authentication", color = colors.foreground)
                            }

                            if (useSasl) {
                                OutlinedTextField(
                                        value = saslUsername,
                                        onValueChange = { saslUsername = it },
                                        label = { Text("SASL Username") },
                                        modifier = Modifier.fillMaxWidth(),
                                        singleLine = true
                                )
                                OutlinedTextField(
                                        value = saslPassword,
                                        onValueChange = { saslPassword = it },
                                        label = { Text("SASL Password") },
                                        modifier = Modifier.fillMaxWidth(),
                                        singleLine = true,
                                        visualTransformation = PasswordVisualTransformation()
                                )
                            }
                        }

                        // IRC-only settings (hide for Matrix)
                        if (serverType != ServerType.MATRIX) {
                            HorizontalDivider(color = colors.border)

                            OutlinedTextField(
                                    value = autoJoinChannels,
                                    onValueChange = { autoJoinChannels = it },
                                    label = { Text("Auto-join Channels") },
                                    placeholder = { Text("#channel1, #channel2") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    supportingText = {
                                        Text(
                                                "Channels to join automatically after connecting",
                                                style = MaterialTheme.typography.bodySmall,
                                                color = colors.comment
                                        )
                                    }
                            )

                            OutlinedTextField(
                                    value = onConnectCommands,
                                    onValueChange = { onConnectCommands = it },
                                    label = { Text("Commands on Connect") },
                                    placeholder = { Text("/msg NickServ IDENTIFY password") },
                                    modifier = Modifier.fillMaxWidth(),
                                    maxLines = 5,
                                    supportingText = {
                                        Text(
                                                "IRC commands to execute after connecting (one per line)",
                                                style = MaterialTheme.typography.bodySmall,
                                                color = colors.comment
                                        )
                                    }
                            )

                            HorizontalDivider(color = colors.border)

                            Text(
                                    "Proxy Settings",
                                    style = MaterialTheme.typography.titleSmall,
                                    color = colors.cyan
                            )

                            Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text("Proxy Type", color = colors.foreground)
                                Box {
                                    var expanded by remember { mutableStateOf(false) }
                                    TextButton(onClick = { expanded = true }) {
                                        Text(proxyType.name, color = colors.cyan)
                                        Icon(Icons.Default.ArrowDropDown, null, tint = colors.cyan)
                                    }
                                    DropdownMenu(
                                            expanded = expanded,
                                            onDismissRequest = { expanded = false },
                                            containerColor = colors.windowBackground
                                    ) {
                                        ProxyType.entries.forEach { type ->
                                            DropdownMenuItem(
                                                    text = {
                                                        Text(type.name, color = colors.foreground)
                                                    },
                                                    onClick = {
                                                        proxyType = type
                                                        expanded = false
                                                    }
                                            )
                                        }
                                    }
                                }
                            }

                            if (proxyType != ProxyType.NONE) {
                                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                    OutlinedTextField(
                                            value = proxyHost,
                                            onValueChange = { proxyHost = it },
                                            label = { Text("Proxy Host") },
                                            modifier = Modifier.weight(1f),
                                            singleLine = true
                                    )
                                    OutlinedTextField(
                                            value = proxyPort,
                                            onValueChange = {
                                                proxyPort = it.filter { c -> c.isDigit() }
                                            },
                                            label = { Text("Port") },
                                            modifier = Modifier.width(100.dp),
                                            singleLine = true
                                    )
                                }

                                OutlinedTextField(
                                        value = proxyUsername,
                                        onValueChange = { proxyUsername = it },
                                        label = { Text("Proxy Username (Optional)") },
                                        modifier = Modifier.fillMaxWidth(),
                                        singleLine = true
                                )

                                OutlinedTextField(
                                        value = proxyPassword,
                                        onValueChange = { proxyPassword = it },
                                        label = { Text("Proxy Password (Optional)") },
                                        modifier = Modifier.fillMaxWidth(),
                                        singleLine = true,
                                        visualTransformation = PasswordVisualTransformation()
                                )
                            }
                        } // End IRC-only settings
                    }
                    CustomVerticalScrollbar(
                            scrollState = scrollState,
                            modifier = Modifier.align(Alignment.CenterEnd)
                    )
                }
            },
            confirmButton = {
                Button(
                        onClick = {
                            if (hostname.isNotBlank() &&
                                            nickname.isNotBlank() && // Check logic logic below
                                            username.isNotBlank()
                            ) {
                                // Logic handled in onClick
                            }

                            val isValid =
                                    if (serverType == ServerType.MATRIX) {
                                        matrixHomeserver.isNotBlank() &&
                                                matrixAccessToken.isNotBlank() &&
                                                username.isNotBlank()
                                    } else {
                                        hostname.isNotBlank() &&
                                                nickname.isNotBlank() &&
                                                username.isNotBlank()
                                    }

                            if (isValid) {
                                val config =
                                        ServerConfig(
                                                id = System.currentTimeMillis(),
                                                serverName =
                                                        serverName.ifBlank {
                                                            if (serverType == ServerType.MATRIX)
                                                                    matrixHomeserver
                                                            else hostname
                                                        },
                                                hostname = hostname,
                                                port = port.toIntOrNull() ?: 6697,
                                                useSsl = useSsl,
                                                acceptSelfSignedCerts = acceptSelfSignedCerts,
                                                serverPassword = serverPassword.ifBlank { null },
                                                nickname = nickname,
                                                altNickname = altNickname,
                                                username = username,
                                                realName = realName,
                                                nickServPassword =
                                                        nickServPassword.ifBlank { null },
                                                useSasl = useSasl,
                                                saslUsername =
                                                        if (useSasl) saslUsername.ifBlank { null }
                                                        else null,
                                                saslPassword =
                                                        if (useSasl) saslPassword.ifBlank { null }
                                                        else null,
                                                autoJoinChannels = autoJoinChannels,
                                                onConnectCommands = onConnectCommands,
                                                proxyType = proxyType,
                                                proxyHost = proxyHost,
                                                proxyPort = proxyPort.toIntOrNull() ?: 1080,
                                                proxyUsername = proxyUsername.ifBlank { null },
                                                proxyPassword = proxyPassword.ifBlank { null },
                                                type = serverType,
                                                matrixHomeserver =
                                                        matrixHomeserver.ifBlank { null },
                                                matrixAccessToken =
                                                        matrixAccessToken.ifBlank { null }
                                        )
                                onConnect(config)
                            }
                        },
                        enabled =
                                if (serverType == ServerType.MATRIX) {
                                    matrixHomeserver.isNotBlank() &&
                                            matrixAccessToken.isNotBlank() &&
                                            username.isNotBlank()
                                } else {
                                    hostname.isNotBlank() &&
                                            nickname.isNotBlank() &&
                                            altNickname.isNotBlank() &&
                                            username.isNotBlank() &&
                                            realName.isNotBlank()
                                },
                        colors = ButtonDefaults.buttonColors(containerColor = colors.green)
                ) { Text("Connect") }
            },
            dismissButton = {
                TextButton(onClick = onDismiss) { Text("Cancel", color = colors.comment) }
            }
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun JoinChannelDialog(onDismiss: () -> Unit, onJoin: (String) -> Unit) {
    val colors = AppColors.current
    var channelName by remember { mutableStateOf("") }

    AlertDialog(
            onDismissRequest = onDismiss,
            containerColor = colors.windowBackground,
            title = { Text("Join Channel", color = colors.foreground) },
            text = {
                OutlinedTextField(
                        value = channelName,
                        onValueChange = { channelName = it },
                        label = { Text("Channel Name") },
                        placeholder = { Text("#channel") },
                        modifier = Modifier.fillMaxWidth(),
                        singleLine = true
                )
            },
            confirmButton = {
                Button(
                        onClick = {
                            if (channelName.isNotBlank()) {
                                val formatted =
                                        if (channelName.startsWith("#")) channelName
                                        else "#$channelName"
                                onJoin(formatted)
                            }
                        },
                        colors = ButtonDefaults.buttonColors(containerColor = colors.green)
                ) { Text("Join") }
            },
            dismissButton = {
                TextButton(onClick = onDismiss) { Text("Cancel", color = colors.comment) }
            }
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun SettingsDialog(
        userPreferences: UserPreferences,
        onFontSizeChange: (FontSize) -> Unit,
        onTimestampFormatChange: (TimestampFormat) -> Unit,
        onUrlImageDisplayModeChange: (UrlImageDisplayMode) -> Unit,
        onColoredNicknamesChange: (Boolean) -> Unit,
        onJoinPartQuitModeChange: (JoinPartQuitDisplayMode) -> Unit,
        onSmartHideMinutesChange: (Int) -> Unit,
        onSplitViewEnabledChange: (Boolean) -> Unit,
        onSpellCheckEnabledChange: (Boolean) -> Unit,
        onSpellCheckLanguageChange: (String) -> Unit,
        onThemeChange: (String) -> Unit,
        onOpenThemesFolder: () -> Unit,
        onSoundAlertsEnabledChange: (Boolean) -> Unit,
        onSoundVolumeChange: (Float) -> Unit,
        onSoundOnMentionChange: (Boolean) -> Unit,
        onSoundOnPrivateMessageChange: (Boolean) -> Unit,
        onSoundOnHighlightChange: (Boolean) -> Unit, // Added missing param
        onLoggingEnabledChange: (Boolean) -> Unit,
        onHistoryReplayLinesChange: (Int) -> Unit,
        onProcessLinkPreviewsFromOthersChange: (Boolean) -> Unit,
        onUseProxyForPreviewsChange: (Boolean) -> Unit,
        onPastebinThresholdChange: (Int) -> Unit,
        onImgbbApiKeyChange: (String) -> Unit,
        onUrlShorteningEnabledChange: (Boolean) -> Unit,
        onUrlShorteningThresholdChange: (Int) -> Unit,
        onDismiss: () -> Unit
) {
    val colors = AppColors.current
    var fontExpanded by remember { mutableStateOf(false) }
    var timestampExpanded by remember { mutableStateOf(false) }
    var matchExpanded by remember { mutableStateOf(false) }
    var displayModeExpanded by remember { mutableStateOf(false) }
    var languageExpanded by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    val scrollState = rememberScrollState()

    AlertDialog(
            onDismissRequest = onDismiss,
            containerColor = colors.windowBackground,
            title = { Text("Settings", color = colors.foreground) },
            text = {
                val scope = rememberCoroutineScope()
                val scrollState = rememberScrollState()
                Box(modifier = Modifier.width(420.dp).heightIn(max = 500.dp)) {
                    Column(
                            modifier =
                                    Modifier.fillMaxWidth()
                                            .verticalScroll(scrollState)
                                            .padding(end = 12.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        // Display Section
                        Text(
                                text = "Display",
                                style = MaterialTheme.typography.titleSmall,
                                color = colors.cyan
                        )

                        HorizontalDivider(color = colors.border)

                        // Theme Settings
                        Text(
                                text = "Appearance",
                                style = MaterialTheme.typography.titleSmall,
                                color = colors.cyan,
                                modifier = Modifier.padding(top = 8.dp)
                        )

                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("Theme", color = colors.foreground)
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                // Theme Dropdown
                                Box {
                                    var themeExpanded by remember { mutableStateOf(false) }
                                    TextButton(onClick = { themeExpanded = true }) {
                                        Text(userPreferences.theme, color = colors.cyan)
                                        Icon(Icons.Default.ArrowDropDown, null, tint = colors.cyan)
                                    }
                                    DropdownMenu(
                                            expanded = themeExpanded,
                                            onDismissRequest = { themeExpanded = false },
                                            containerColor = colors.windowBackground
                                    ) {
                                        val themes =
                                                com.loungecat.irc.ui.theme.ThemeManager
                                                        .getAvailableThemes()
                                        themes.forEach { themeName ->
                                            DropdownMenuItem(
                                                    text = {
                                                        Text(themeName, color = colors.foreground)
                                                    },
                                                    onClick = {
                                                        onThemeChange(themeName)
                                                        themeExpanded = false
                                                    }
                                            )
                                        }
                                        HorizontalDivider(color = colors.border)
                                        DropdownMenuItem(
                                                text = {
                                                    Text("Refresh Themes", color = colors.comment)
                                                },
                                                onClick = {
                                                    com.loungecat.irc.ui.theme.ThemeManager
                                                            .loadThemes()
                                                    themeExpanded = false
                                                }
                                        )
                                    }
                                }
                                IconButton(
                                        onClick = onOpenThemesFolder,
                                        modifier = Modifier.size(28.dp)
                                ) {
                                    Icon(
                                            imageVector = Icons.Default.FolderOpen,
                                            contentDescription = "Open Themes Folder",
                                            tint = colors.comment,
                                            modifier = Modifier.size(18.dp)
                                    )
                                }
                            }
                        }

                        // Font Size
                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("Font Size", color = colors.foreground)
                            Box {
                                TextButton(onClick = { fontExpanded = true }) {
                                    Text(userPreferences.fontSize.name, color = colors.cyan)
                                    Icon(Icons.Default.ArrowDropDown, null, tint = colors.cyan)
                                }
                                DropdownMenu(
                                        expanded = fontExpanded,
                                        onDismissRequest = { fontExpanded = false },
                                        containerColor = colors.windowBackground
                                ) {
                                    FontSize.entries.forEach { size ->
                                        DropdownMenuItem(
                                                text = {
                                                    Text(size.name, color = colors.foreground)
                                                },
                                                onClick = {
                                                    onFontSizeChange(size)
                                                    fontExpanded = false
                                                }
                                        )
                                    }
                                }
                            }
                        }

                        // Timestamp Format
                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("Timestamp Format", color = colors.foreground)
                            Box {
                                TextButton(onClick = { timestampExpanded = true }) {
                                    val displayName =
                                            when (userPreferences.timestampFormat) {
                                                TimestampFormat.HOURS_12 -> "12-hour"
                                                TimestampFormat.HOURS_24 -> "24-hour"
                                                TimestampFormat.RELATIVE -> "Relative"
                                            }
                                    Text(displayName, color = colors.cyan)
                                    Icon(Icons.Default.ArrowDropDown, null, tint = colors.cyan)
                                }
                                DropdownMenu(
                                        expanded = timestampExpanded,
                                        onDismissRequest = { timestampExpanded = false },
                                        containerColor = colors.windowBackground
                                ) {
                                    DropdownMenuItem(
                                            text = {
                                                Text("12-hour (3:45 PM)", color = colors.foreground)
                                            },
                                            onClick = {
                                                onTimestampFormatChange(TimestampFormat.HOURS_12)
                                                timestampExpanded = false
                                            }
                                    )
                                    DropdownMenuItem(
                                            text = {
                                                Text("24-hour (15:45)", color = colors.foreground)
                                            },
                                            onClick = {
                                                onTimestampFormatChange(TimestampFormat.HOURS_24)
                                                timestampExpanded = false
                                            }
                                    )
                                    DropdownMenuItem(
                                            text = {
                                                Text("Relative (5m ago)", color = colors.foreground)
                                            },
                                            onClick = {
                                                onTimestampFormatChange(TimestampFormat.RELATIVE)
                                                timestampExpanded = false
                                            }
                                    )
                                }
                            }
                        }

                        // URL/Image Display Mode
                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("URL/Image Previews", color = colors.foreground)
                            Box {
                                TextButton(onClick = { displayModeExpanded = true }) {
                                    val displayName =
                                            when (userPreferences.urlImageDisplayMode) {
                                                UrlImageDisplayMode.INLINE -> "Inline"
                                                UrlImageDisplayMode.COMPACT -> "Compact"
                                                UrlImageDisplayMode.DISABLED -> "Disabled"
                                            }
                                    Text(displayName, color = colors.cyan)
                                    Icon(Icons.Default.ArrowDropDown, null, tint = colors.cyan)
                                }
                                DropdownMenu(
                                        expanded = displayModeExpanded,
                                        onDismissRequest = { displayModeExpanded = false },
                                        containerColor = colors.windowBackground
                                ) {
                                    DropdownMenuItem(
                                            text = {
                                                Text(
                                                        "Inline (always show)",
                                                        color = colors.foreground
                                                )
                                            },
                                            onClick = {
                                                onUrlImageDisplayModeChange(
                                                        UrlImageDisplayMode.INLINE
                                                )
                                                displayModeExpanded = false
                                            }
                                    )
                                    DropdownMenuItem(
                                            text = {
                                                Text(
                                                        "Compact (click to expand)",
                                                        color = colors.foreground
                                                )
                                            },
                                            onClick = {
                                                onUrlImageDisplayModeChange(
                                                        UrlImageDisplayMode.COMPACT
                                                )
                                                displayModeExpanded = false
                                            }
                                    )
                                    DropdownMenuItem(
                                            text = {
                                                Text(
                                                        "Disabled (no previews)",
                                                        color = colors.foreground
                                                )
                                            },
                                            onClick = {
                                                onUrlImageDisplayModeChange(
                                                        UrlImageDisplayMode.DISABLED
                                                )
                                                displayModeExpanded = false
                                            }
                                    )
                                }
                            }
                        }

                        // Process Link Previews From Others
                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text("Process Previews from Others", color = colors.foreground)
                                Text(
                                        "Automatically load previews for links sent by other users",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = colors.comment
                                )
                            }
                            Switch(
                                    checked = userPreferences.processLinkPreviewsFromOthers,
                                    onCheckedChange = onProcessLinkPreviewsFromOthersChange,
                                    colors =
                                            SwitchDefaults.colors(
                                                    checkedThumbColor = colors.green,
                                                    checkedTrackColor =
                                                            colors.green.copy(alpha = 0.5f)
                                            )
                            )
                        }

                        // Use Proxy for Previews
                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text("Use Proxy for Previews", color = colors.foreground)
                                Text(
                                        "Route preview requests through server proxy (keeps IP hidden)",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = colors.comment
                                )
                            }
                            Switch(
                                    checked = userPreferences.useProxyForPreviews,
                                    onCheckedChange = onUseProxyForPreviewsChange,
                                    colors =
                                            SwitchDefaults.colors(
                                                    checkedThumbColor = colors.green,
                                                    checkedTrackColor =
                                                            colors.green.copy(alpha = 0.5f)
                                            )
                            )
                        }

                        // Users
                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text("Paste Service Threshold (chars)", color = colors.foreground)
                                Text(
                                        "Automatically upload messages longer than this",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = colors.comment
                                )
                            }
                            OutlinedTextField(
                                    value = userPreferences.pastebinThreshold.toString(),
                                    onValueChange = {
                                        val newValue = it.filter { char -> char.isDigit() }
                                        if (newValue.isNotEmpty()) {
                                            onPastebinThresholdChange(newValue.toInt())
                                        }
                                    },
                                    modifier = Modifier.width(100.dp),
                                    singleLine = true,
                                    colors =
                                            OutlinedTextFieldDefaults.colors(
                                                    focusedBorderColor = colors.cyan,
                                                    unfocusedBorderColor = colors.border,
                                                    focusedTextColor = colors.foreground,
                                                    unfocusedTextColor = colors.foreground,
                                                    cursorColor = colors.cyan
                                            )
                            )
                        }

                        HorizontalDivider(color = colors.border)

                        // External Services (ImgBB & URL Shortener)
                        Text(
                                text = "External Services",
                                style = MaterialTheme.typography.titleSmall,
                                color = colors.cyan,
                                fontWeight = FontWeight.Bold,
                                modifier = Modifier.padding(top = 8.dp)
                        )

                        // ImgBB API Key
                        Column {
                            Text("ImgBB API Key (for image uploads)", color = colors.foreground)
                            Spacer(modifier = Modifier.height(4.dp))
                            OutlinedTextField(
                                    value = userPreferences.imgbbApiKey ?: "",
                                    onValueChange = { onImgbbApiKeyChange(it) },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    visualTransformation = PasswordVisualTransformation(),
                                    placeholder = { Text("Enter API Key", color = colors.comment) },
                                    colors =
                                            OutlinedTextFieldDefaults.colors(
                                                    focusedBorderColor = colors.cyan,
                                                    unfocusedBorderColor = colors.border,
                                                    focusedTextColor = colors.foreground,
                                                    unfocusedTextColor = colors.foreground,
                                                    cursorColor = colors.cyan
                                            )
                            )
                            Text(
                                    "Get key from api.imgbb.com",
                                    style = MaterialTheme.typography.bodySmall,
                                    color = colors.comment,
                                    modifier = Modifier.padding(top = 2.dp)
                            )
                        }

                        // URL Shortening
                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column(modifier = Modifier.weight(1f)) {
                                Text("Enable URL Shortening", color = colors.foreground)
                                Text(
                                        "Automatically shorten long URLs",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = colors.comment
                                )
                            }
                            Switch(
                                    checked = userPreferences.urlShorteningEnabled,
                                    onCheckedChange = onUrlShorteningEnabledChange,
                                    colors =
                                            SwitchDefaults.colors(
                                                    checkedThumbColor = colors.green,
                                                    checkedTrackColor =
                                                            colors.green.copy(alpha = 0.5f)
                                            )
                            )
                        }

                        if (userPreferences.urlShorteningEnabled) {
                            Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text("Shorten Threshold (chars)", color = colors.foreground)
                                    Text(
                                            "URLs longer than this will be shortened",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = colors.comment
                                    )
                                }
                                OutlinedTextField(
                                        value = userPreferences.urlShorteningThreshold.toString(),
                                        onValueChange = {
                                            val newValue = it.filter { char -> char.isDigit() }
                                            if (newValue.isNotEmpty()) {
                                                onUrlShorteningThresholdChange(newValue.toInt())
                                            }
                                        },
                                        modifier = Modifier.width(100.dp),
                                        singleLine = true,
                                        colors =
                                                OutlinedTextFieldDefaults.colors(
                                                        focusedBorderColor = colors.cyan,
                                                        unfocusedBorderColor = colors.border,
                                                        focusedTextColor = colors.foreground,
                                                        unfocusedTextColor = colors.foreground,
                                                        cursorColor = colors.cyan
                                                )
                                )
                            }
                        }

                        HorizontalDivider(color = colors.border)

                        // Split View
                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column {
                                Text("Split View", color = colors.foreground)
                                Text(
                                        "Show two channels side-by-side",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = colors.comment
                                )
                            }
                            Switch(
                                    checked = userPreferences.splitViewEnabled,
                                    onCheckedChange = onSplitViewEnabledChange,
                                    colors =
                                            SwitchDefaults.colors(
                                                    checkedThumbColor = colors.green,
                                                    checkedTrackColor =
                                                            colors.green.copy(alpha = 0.5f)
                                            )
                            )
                        }

                        // Colored Nicknames
                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("Colored Nicknames", color = colors.foreground)
                            Switch(
                                    checked = userPreferences.coloredNicknames,
                                    onCheckedChange = onColoredNicknamesChange,
                                    colors =
                                            SwitchDefaults.colors(
                                                    checkedThumbColor = colors.green,
                                                    checkedTrackColor =
                                                            colors.green.copy(alpha = 0.5f)
                                            )
                            )
                        }

                        // Join/Part/Quit Display Mode
                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Column {
                                Text("Join/Part/Quit Messages", color = colors.foreground)
                                Text(
                                        "How to display user join/leave events",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = colors.comment
                                )
                            }
                            Box {
                                var jpqExpanded by remember { mutableStateOf(false) }
                                val displayName =
                                        when (userPreferences.joinPartQuitMode) {
                                            JoinPartQuitDisplayMode.SHOW_ALL -> "Show All"
                                            JoinPartQuitDisplayMode.HIDE_ALL -> "Hide All"
                                            JoinPartQuitDisplayMode.GROUPED -> "Group Together"
                                            JoinPartQuitDisplayMode.SMART_HIDE -> "Smart Hide"
                                        }
                                TextButton(onClick = { jpqExpanded = true }) {
                                    Text(displayName, color = colors.cyan)
                                    Icon(Icons.Default.ArrowDropDown, null, tint = colors.cyan)
                                }
                                DropdownMenu(
                                        expanded = jpqExpanded,
                                        onDismissRequest = { jpqExpanded = false },
                                        containerColor = colors.windowBackground
                                ) {
                                    DropdownMenuItem(
                                            text = {
                                                Column {
                                                    Text("Show All", color = colors.foreground)
                                                    Text(
                                                            "Show all join/part/quit messages",
                                                            style =
                                                                    MaterialTheme.typography
                                                                            .bodySmall,
                                                            color = colors.comment
                                                    )
                                                }
                                            },
                                            onClick = {
                                                onJoinPartQuitModeChange(
                                                        JoinPartQuitDisplayMode.SHOW_ALL
                                                )
                                                jpqExpanded = false
                                            }
                                    )
                                    DropdownMenuItem(
                                            text = {
                                                Column {
                                                    Text("Hide All", color = colors.foreground)
                                                    Text(
                                                            "Hide all join/part/quit messages",
                                                            style =
                                                                    MaterialTheme.typography
                                                                            .bodySmall,
                                                            color = colors.comment
                                                    )
                                                }
                                            },
                                            onClick = {
                                                onJoinPartQuitModeChange(
                                                        JoinPartQuitDisplayMode.HIDE_ALL
                                                )
                                                jpqExpanded = false
                                            }
                                    )
                                    DropdownMenuItem(
                                            text = {
                                                Column {
                                                    Text(
                                                            "Group Together",
                                                            color = colors.foreground
                                                    )
                                                    Text(
                                                            "Collapse consecutive events",
                                                            style =
                                                                    MaterialTheme.typography
                                                                            .bodySmall,
                                                            color = colors.comment
                                                    )
                                                }
                                            },
                                            onClick = {
                                                onJoinPartQuitModeChange(
                                                        JoinPartQuitDisplayMode.GROUPED
                                                )
                                                jpqExpanded = false
                                            }
                                    )
                                    DropdownMenuItem(
                                            text = {
                                                Column {
                                                    Text("Smart Hide", color = colors.foreground)
                                                    Text(
                                                            "Only show for active users",
                                                            style =
                                                                    MaterialTheme.typography
                                                                            .bodySmall,
                                                            color = colors.comment
                                                    )
                                                }
                                            },
                                            onClick = {
                                                onJoinPartQuitModeChange(
                                                        JoinPartQuitDisplayMode.SMART_HIDE
                                                )
                                                jpqExpanded = false
                                            }
                                    )
                                }
                            }
                        }

                        // Smart Hide Duration (only shown when Smart Hide mode is active)
                        if (userPreferences.joinPartQuitMode == JoinPartQuitDisplayMode.SMART_HIDE
                        ) {
                            Row(
                                    modifier = Modifier.fillMaxWidth().padding(start = 16.dp),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically
                            ) {
                                Column {
                                    Text("Activity Threshold", color = colors.foreground)
                                    Text(
                                            "Only show for users who spoke recently",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = colors.comment
                                    )
                                }
                                Box {
                                    var smartHideExpanded by remember { mutableStateOf(false) }
                                    TextButton(onClick = { smartHideExpanded = true }) {
                                        Text(
                                                "${userPreferences.smartHideMinutes} min",
                                                color = colors.cyan
                                        )
                                        Icon(Icons.Default.ArrowDropDown, null, tint = colors.cyan)
                                    }
                                    DropdownMenu(
                                            expanded = smartHideExpanded,
                                            onDismissRequest = { smartHideExpanded = false },
                                            containerColor = colors.windowBackground
                                    ) {
                                        listOf(5, 10, 15, 30, 60).forEach { minutes ->
                                            DropdownMenuItem(
                                                    text = {
                                                        Text(
                                                                "$minutes minutes",
                                                                color = colors.foreground
                                                        )
                                                    },
                                                    onClick = {
                                                        onSmartHideMinutesChange(minutes)
                                                        smartHideExpanded = false
                                                    }
                                            )
                                        }
                                    }
                                }
                            }
                        }

                        // Spell Checking
                        Text(
                                text = "Spell Checking",
                                style = MaterialTheme.typography.titleSmall,
                                color = colors.cyan,
                                modifier = Modifier.padding(top = 8.dp)
                        )

                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("Enable Spell Check", color = colors.foreground)
                            Switch(
                                    checked = userPreferences.spellCheckEnabled,
                                    onCheckedChange = onSpellCheckEnabledChange,
                                    colors =
                                            SwitchDefaults.colors(
                                                    checkedThumbColor = colors.green,
                                                    checkedTrackColor =
                                                            colors.green.copy(alpha = 0.5f)
                                            )
                            )
                        }

                        if (userPreferences.spellCheckEnabled) {
                            Row(
                                    modifier = Modifier.fillMaxWidth(),
                                    horizontalArrangement = Arrangement.SpaceBetween,
                                    verticalAlignment = Alignment.CenterVertically
                            ) {
                                Text("Language", color = colors.foreground)
                                Box {
                                    TextButton(onClick = { languageExpanded = true }) {
                                        Text(
                                                userPreferences.spellCheckLanguage,
                                                color = colors.cyan
                                        )
                                        Icon(Icons.Default.ArrowDropDown, null, tint = colors.cyan)
                                    }
                                    DropdownMenu(
                                            expanded = languageExpanded,
                                            onDismissRequest = { languageExpanded = false },
                                            containerColor = colors.windowBackground
                                    ) {
                                        // Use SpellChecker available languages or fallback
                                        val languages = listOf("en_US", "pl", "nl", "de")
                                        languages.forEach { lang ->
                                            DropdownMenuItem(
                                                    text = {
                                                        Text(lang, color = colors.foreground)
                                                    },
                                                    onClick = {
                                                        onSpellCheckLanguageChange(lang)
                                                        languageExpanded = false
                                                    }
                                            )
                                        }
                                    }
                                }
                            }
                        }

                        HorizontalDivider(color = colors.border)

                        Text(
                                text = "Sound Alerts",
                                style = MaterialTheme.typography.titleSmall,
                                color = colors.cyan,
                                fontWeight = FontWeight.Bold,
                                modifier = Modifier.padding(top = 8.dp)
                        )

                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Text("Enable Sounds", color = colors.foreground)
                            Switch(
                                    checked = userPreferences.soundAlertsEnabled,
                                    onCheckedChange = onSoundAlertsEnabledChange,
                                    colors =
                                            SwitchDefaults.colors(
                                                    checkedThumbColor = colors.windowBackground,
                                                    checkedTrackColor = colors.green,
                                                    uncheckedThumbColor = colors.comment,
                                                    uncheckedTrackColor = colors.background
                                            )
                            )
                        }

                        if (userPreferences.soundAlertsEnabled) {
                            Column(modifier = Modifier.padding(start = 16.dp)) {
                                Text("Volume", color = colors.foreground)
                                Slider(
                                        value = userPreferences.soundVolume,
                                        onValueChange = onSoundVolumeChange,
                                        valueRange = 0f..1f,
                                        colors =
                                                SliderDefaults.colors(
                                                        thumbColor = colors.cyan,
                                                        activeTrackColor = colors.cyan,
                                                        inactiveTrackColor = colors.background
                                                )
                                )

                                Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.SpaceBetween
                                ) {
                                    Text("On Mention", color = colors.foreground)
                                    Checkbox(
                                            checked = userPreferences.soundOnMention,
                                            onCheckedChange = onSoundOnMentionChange
                                    )
                                }
                                Row(
                                        modifier = Modifier.fillMaxWidth(),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.SpaceBetween
                                ) {
                                    Text("On Private Message", color = colors.foreground)
                                    Checkbox(
                                            checked = userPreferences.soundOnPrivateMessage,
                                            onCheckedChange = onSoundOnPrivateMessageChange
                                    )
                                }
                            }
                        }

                        HorizontalDivider(color = colors.border)

                        Text(
                                text = "Logging & History",
                                style = MaterialTheme.typography.titleSmall,
                                color = colors.cyan,
                                fontWeight = FontWeight.Bold,
                                modifier = Modifier.padding(top = 8.dp)
                        )

                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Column {
                                Text("Enable Text Logging", color = colors.foreground)
                                Text(
                                        "Save chat logs to disk",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = colors.comment
                                )
                            }
                            Switch(
                                    checked = userPreferences.loggingEnabled,
                                    onCheckedChange = onLoggingEnabledChange,
                                    colors =
                                            SwitchDefaults.colors(
                                                    checkedThumbColor = colors.windowBackground,
                                                    checkedTrackColor = colors.green,
                                                    uncheckedThumbColor = colors.comment,
                                                    uncheckedTrackColor = colors.background
                                            )
                            )
                        }

                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                verticalAlignment = Alignment.CenterVertically,
                                horizontalArrangement = Arrangement.SpaceBetween
                        ) {
                            Column {
                                Text("History Replay Lines", color = colors.foreground)
                                Text(
                                        "Messages to load on join",
                                        style = MaterialTheme.typography.bodySmall,
                                        color = colors.comment
                                )
                            }
                            // Using a box with dropdown for strict values or text field?
                            // Text field is better for custom number, but Slider is safer.
                            // Let's use a Box with basic selection for now or a simple
                            // OutlinedTextField if available.
                            // Given the context, I'll use a simple dropdown for common values to
                            // match other UI patterns here if easy, or just a Slider.
                            // A Slider 0..1000 seems reasonable.

                            Box(
                                    contentAlignment = Alignment.CenterEnd,
                                    modifier = Modifier.width(150.dp)
                            ) {
                                Text(
                                        "${userPreferences.historyReplayLines} lines",
                                        color = colors.cyan
                                )
                            }
                        }

                        Slider(
                                value = userPreferences.historyReplayLines.toFloat(),
                                onValueChange = { onHistoryReplayLinesChange(it.toInt()) },
                                valueRange = 0f..2000f,
                                steps = 19, // 100 increments roughly
                                colors =
                                        SliderDefaults.colors(
                                                thumbColor = colors.cyan,
                                                activeTrackColor = colors.cyan,
                                                inactiveTrackColor = colors.background
                                        )
                        )
                    }
                    CustomVerticalScrollbar(
                            scrollState = scrollState,
                            modifier = Modifier.align(Alignment.CenterEnd)
                    )
                }
            },
            confirmButton = {
                TextButton(onClick = onDismiss) { Text("Close", color = colors.cyan) }
            }
    )
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun EditServerDialog(
        config: ServerConfig,
        onDismiss: () -> Unit,
        onSave: (ServerConfig, reconnect: Boolean) -> Unit
) {
    val colors = AppColors.current

    var matrixHomeserver by remember { mutableStateOf(config.matrixHomeserver ?: "") }
    var matrixAccessToken by remember { mutableStateOf(config.matrixAccessToken ?: "") }

    var serverName by remember { mutableStateOf(config.serverName) }
    var hostname by remember { mutableStateOf(config.hostname) }
    var port by remember { mutableStateOf(config.port.toString()) }
    var useSsl by remember { mutableStateOf(config.useSsl) }
    var acceptSelfSignedCerts by remember { mutableStateOf(config.acceptSelfSignedCerts) }
    var serverPassword by remember { mutableStateOf(config.serverPassword ?: "") }
    var nickname by remember { mutableStateOf(config.nickname) }
    var altNickname by remember { mutableStateOf(config.altNickname) }
    var username by remember { mutableStateOf(config.username) }
    var realName by remember { mutableStateOf(config.realName) }
    var useSasl by remember { mutableStateOf(config.useSasl) }
    var saslUsername by remember { mutableStateOf(config.saslUsername ?: "") }
    var saslPassword by remember { mutableStateOf(config.saslPassword ?: "") }
    var nickServPassword by remember { mutableStateOf(config.nickServPassword ?: "") }
    var autoJoinChannels by remember { mutableStateOf(config.autoJoinChannels) }
    var onConnectCommands by remember { mutableStateOf(config.onConnectCommands) }
    var proxyType by remember { mutableStateOf(config.proxyType) }
    var proxyHost by remember { mutableStateOf(config.proxyHost) }
    var proxyPort by remember { mutableStateOf(config.proxyPort.toString()) }
    var proxyUsername by remember { mutableStateOf(config.proxyUsername ?: "") }
    var proxyPassword by remember { mutableStateOf(config.proxyPassword ?: "") }

    AlertDialog(
            onDismissRequest = onDismiss,
            containerColor = colors.windowBackground,
            title = { Text("Edit Server", color = colors.foreground) },
            text = {
                val scope = rememberCoroutineScope()
                val scrollState = rememberScrollState()
                Box(modifier = Modifier.width(420.dp).heightIn(max = 500.dp)) {
                    Column(
                            modifier =
                                    Modifier.fillMaxWidth()
                                            .verticalScroll(scrollState)
                                            .padding(end = 12.dp),
                            verticalArrangement = Arrangement.spacedBy(12.dp)
                    ) {
                        OutlinedTextField(
                                value = serverName,
                                onValueChange = { serverName = it },
                                label = { Text("Server Name") },
                                placeholder = {
                                    Text(
                                            if (config.type == ServerType.MATRIX) "e.g., Matrix"
                                            else "e.g., Libera Chat"
                                    )
                                },
                                modifier = Modifier.fillMaxWidth(),
                                singleLine = true
                        )

                        if (config.type == ServerType.MATRIX) {
                            OutlinedTextField(
                                    value = matrixHomeserver,
                                    onValueChange = { matrixHomeserver = it },
                                    label = { Text("Homeserver URL") },
                                    placeholder = { Text("https://matrix.org") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = matrixAccessToken,
                                    onValueChange = { matrixAccessToken = it },
                                    label = { Text("Password") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    visualTransformation = PasswordVisualTransformation()
                            )

                            OutlinedTextField(
                                    value = username,
                                    onValueChange = { username = it },
                                    label = { Text("Username (@user:domain)") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = nickname,
                                    onValueChange = { nickname = it },
                                    label = { Text("Display Name (Optional)") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )
                        } else {
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                OutlinedTextField(
                                        value = hostname,
                                        onValueChange = { hostname = it },
                                        label = { Text("Hostname") },
                                        placeholder = { Text("irc.libera.chat") },
                                        modifier = Modifier.weight(1f),
                                        singleLine = true
                                )
                                OutlinedTextField(
                                        value = port,
                                        onValueChange = { port = it.filter { c -> c.isDigit() } },
                                        label = { Text("Port") },
                                        modifier = Modifier.width(100.dp),
                                        singleLine = true
                                )
                            }

                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Checkbox(checked = useSsl, onCheckedChange = { useSsl = it })
                                Text("Use SSL/TLS", color = colors.foreground)
                            }

                            if (useSsl) {
                                Row(
                                        modifier = Modifier.fillMaxWidth().padding(start = 32.dp),
                                        verticalAlignment = Alignment.CenterVertically,
                                        horizontalArrangement = Arrangement.SpaceBetween
                                ) {
                                    Column(modifier = Modifier.weight(1f)) {
                                        Text(
                                                "Accept Self-Signed Certificates",
                                                color = colors.foreground
                                        )
                                        Text(
                                                "Less secure - only for trusted servers",
                                                style = MaterialTheme.typography.bodySmall,
                                                color = colors.orange.copy(alpha = 0.7f)
                                        )
                                    }
                                    Checkbox(
                                            checked = acceptSelfSignedCerts,
                                            onCheckedChange = { acceptSelfSignedCerts = it }
                                    )
                                }
                            }

                            OutlinedTextField(
                                    value = serverPassword,
                                    onValueChange = { serverPassword = it },
                                    label = { Text("Server Password (optional)") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    visualTransformation = PasswordVisualTransformation()
                            )

                            HorizontalDivider(color = colors.border)

                            OutlinedTextField(
                                    value = nickname,
                                    onValueChange = { nickname = it },
                                    label = { Text("Nickname") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = altNickname,
                                    onValueChange = { altNickname = it },
                                    label = { Text("Alternative Nickname") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = username,
                                    onValueChange = { username = it },
                                    label = { Text("Username") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = realName,
                                    onValueChange = { realName = it },
                                    label = { Text("Real Name") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = nickServPassword,
                                    onValueChange = { nickServPassword = it },
                                    label = { Text("NickServ Password (optional)") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    visualTransformation = PasswordVisualTransformation(),
                                    supportingText = {
                                        Text(
                                                "Auto-identify with NickServ on connect",
                                                style = MaterialTheme.typography.bodySmall,
                                                color = colors.comment
                                        )
                                    }
                            )

                            HorizontalDivider(color = colors.border)

                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Checkbox(checked = useSasl, onCheckedChange = { useSasl = it })
                                Text("Use SASL Authentication", color = colors.foreground)
                            }

                            if (useSasl) {
                                OutlinedTextField(
                                        value = saslUsername,
                                        onValueChange = { saslUsername = it },
                                        label = { Text("SASL Username") },
                                        modifier = Modifier.fillMaxWidth(),
                                        singleLine = true
                                )
                                OutlinedTextField(
                                        value = saslPassword,
                                        onValueChange = { saslPassword = it },
                                        label = { Text("SASL Password") },
                                        modifier = Modifier.fillMaxWidth(),
                                        singleLine = true,
                                        visualTransformation = PasswordVisualTransformation()
                                )
                            }
                        }

                        HorizontalDivider(color = colors.border)

                        OutlinedTextField(
                                value = autoJoinChannels,
                                onValueChange = { autoJoinChannels = it },
                                label = { Text("Auto-join Channels") },
                                placeholder = { Text("#channel1, #channel2") },
                                modifier = Modifier.fillMaxWidth(),
                                singleLine = true,
                                supportingText = {
                                    Text(
                                            "Channels to join automatically after connecting",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = colors.comment
                                    )
                                }
                        )

                        OutlinedTextField(
                                value = onConnectCommands,
                                onValueChange = { onConnectCommands = it },
                                label = { Text("Commands on Connect") },
                                placeholder = { Text("/msg NickServ IDENTIFY password") },
                                modifier = Modifier.fillMaxWidth(),
                                maxLines = 5,
                                supportingText = {
                                    Text(
                                            "IRC commands to execute after connecting (one per line)",
                                            style = MaterialTheme.typography.bodySmall,
                                            color = colors.comment
                                    )
                                }
                        )

                        HorizontalDivider(color = colors.border)

                        Text(
                                "Proxy Settings",
                                style = MaterialTheme.typography.titleSmall,
                                color = colors.cyan
                        )

                        Row(
                                modifier = Modifier.fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                        ) {
                            Text("Proxy Type", color = colors.foreground)
                            Box {
                                var expanded by remember { mutableStateOf(false) }
                                TextButton(onClick = { expanded = true }) {
                                    Text(proxyType.name, color = colors.cyan)
                                    Icon(Icons.Default.ArrowDropDown, null, tint = colors.cyan)
                                }
                                DropdownMenu(
                                        expanded = expanded,
                                        onDismissRequest = { expanded = false },
                                        containerColor = colors.windowBackground
                                ) {
                                    ProxyType.entries.forEach { type ->
                                        DropdownMenuItem(
                                                text = {
                                                    Text(type.name, color = colors.foreground)
                                                },
                                                onClick = {
                                                    proxyType = type
                                                    expanded = false
                                                }
                                        )
                                    }
                                }
                            }
                        }

                        if (proxyType != ProxyType.NONE) {
                            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                                OutlinedTextField(
                                        value = proxyHost,
                                        onValueChange = { proxyHost = it },
                                        label = { Text("Proxy Host") },
                                        modifier = Modifier.weight(1f),
                                        singleLine = true
                                )
                                OutlinedTextField(
                                        value = proxyPort,
                                        onValueChange = {
                                            proxyPort = it.filter { c -> c.isDigit() }
                                        },
                                        label = { Text("Port") },
                                        modifier = Modifier.width(100.dp),
                                        singleLine = true
                                )
                            }

                            OutlinedTextField(
                                    value = proxyUsername,
                                    onValueChange = { proxyUsername = it },
                                    label = { Text("Proxy Username (Optional)") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true
                            )

                            OutlinedTextField(
                                    value = proxyPassword,
                                    onValueChange = { proxyPassword = it },
                                    label = { Text("Proxy Password (Optional)") },
                                    modifier = Modifier.fillMaxWidth(),
                                    singleLine = true,
                                    visualTransformation = PasswordVisualTransformation()
                            )
                        }
                    }
                    CustomVerticalScrollbar(
                            scrollState = scrollState,
                            modifier = Modifier.align(Alignment.CenterEnd)
                    )
                }
            },
            confirmButton = {
                Button(
                        onClick = {
                            val isValid =
                                    if (config.type == ServerType.MATRIX) {
                                        matrixHomeserver.isNotBlank() &&
                                                matrixAccessToken.isNotBlank() &&
                                                username.isNotBlank()
                                    } else {
                                        hostname.isNotBlank() &&
                                                nickname.isNotBlank() &&
                                                altNickname.isNotBlank() &&
                                                username.isNotBlank() &&
                                                realName.isNotBlank()
                                    }

                            if (isValid) {
                                val updatedConfig =
                                        ServerConfig(
                                                id = config.id,
                                                serverName =
                                                        serverName.ifBlank {
                                                            if (config.type == ServerType.MATRIX)
                                                                    matrixHomeserver
                                                            else hostname
                                                        },
                                                hostname = hostname,
                                                port = port.toIntOrNull() ?: 6697,
                                                useSsl = useSsl,
                                                acceptSelfSignedCerts = acceptSelfSignedCerts,
                                                serverPassword = serverPassword.ifBlank { null },
                                                nickname = nickname,
                                                altNickname = altNickname,
                                                username = username,
                                                realName = realName,
                                                nickServPassword =
                                                        nickServPassword.ifBlank { null },
                                                useSasl = useSasl,
                                                saslUsername =
                                                        if (useSasl) saslUsername.ifBlank { null }
                                                        else null,
                                                saslPassword =
                                                        if (useSasl) saslPassword.ifBlank { null }
                                                        else null,
                                                autoJoinChannels = autoJoinChannels,
                                                onConnectCommands = onConnectCommands,
                                                proxyType = proxyType,
                                                proxyHost = proxyHost,
                                                proxyPort = proxyPort.toIntOrNull() ?: 1080,
                                                proxyUsername = proxyUsername.ifBlank { null },
                                                proxyPassword = proxyPassword.ifBlank { null },
                                                type = config.type,
                                                matrixHomeserver =
                                                        matrixHomeserver.ifBlank { null },
                                                matrixAccessToken =
                                                        matrixAccessToken.ifBlank { null }
                                        )
                                onSave(updatedConfig, true)
                            }
                        },
                        enabled =
                                if (config.type == ServerType.MATRIX) {
                                    matrixHomeserver.isNotBlank() &&
                                            matrixAccessToken.isNotBlank() &&
                                            username.isNotBlank()
                                } else {
                                    hostname.isNotBlank() &&
                                            nickname.isNotBlank() &&
                                            altNickname.isNotBlank() &&
                                            username.isNotBlank() &&
                                            realName.isNotBlank()
                                },
                        colors = ButtonDefaults.buttonColors(containerColor = colors.green)
                ) { Text("Save & Reconnect") }
            },
            dismissButton = {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    TextButton(onClick = onDismiss) { Text("Cancel", color = colors.comment) }
                    TextButton(
                            onClick = {
                                val isValid =
                                        if (config.type == ServerType.MATRIX) {
                                            matrixHomeserver.isNotBlank() &&
                                                    matrixAccessToken.isNotBlank() &&
                                                    username.isNotBlank()
                                        } else {
                                            hostname.isNotBlank() &&
                                                    nickname.isNotBlank() &&
                                                    altNickname.isNotBlank() &&
                                                    username.isNotBlank() &&
                                                    realName.isNotBlank()
                                        }

                                if (isValid) {
                                    val updatedConfig =
                                            ServerConfig(
                                                    id = config.id,
                                                    serverName =
                                                            serverName.ifBlank {
                                                                if (config.type == ServerType.MATRIX
                                                                )
                                                                        matrixHomeserver
                                                                else hostname
                                                            },
                                                    hostname = hostname,
                                                    port = port.toIntOrNull() ?: 6697,
                                                    useSsl = useSsl,
                                                    acceptSelfSignedCerts = acceptSelfSignedCerts,
                                                    serverPassword =
                                                            serverPassword.ifBlank { null },
                                                    nickname = nickname,
                                                    altNickname = altNickname,
                                                    username = username,
                                                    realName = realName,
                                                    nickServPassword =
                                                            nickServPassword.ifBlank { null },
                                                    useSasl = useSasl,
                                                    saslUsername =
                                                            if (useSasl)
                                                                    saslUsername.ifBlank { null }
                                                            else null,
                                                    saslPassword =
                                                            if (useSasl)
                                                                    saslPassword.ifBlank { null }
                                                            else null,
                                                    autoJoinChannels = autoJoinChannels,
                                                    onConnectCommands = onConnectCommands,
                                                    proxyType = proxyType,
                                                    proxyHost = proxyHost,
                                                    proxyPort = proxyPort.toIntOrNull() ?: 1080,
                                                    proxyUsername = proxyUsername.ifBlank { null },
                                                    proxyPassword = proxyPassword.ifBlank { null },
                                                    type = config.type,
                                                    matrixHomeserver =
                                                            matrixHomeserver.ifBlank { null },
                                                    matrixAccessToken =
                                                            matrixAccessToken.ifBlank { null }
                                            )
                                    onSave(updatedConfig, false)
                                }
                            },
                            enabled =
                                    if (config.type == ServerType.MATRIX) {
                                        matrixHomeserver.isNotBlank() &&
                                                matrixAccessToken.isNotBlank() &&
                                                username.isNotBlank()
                                    } else {
                                        hostname.isNotBlank() &&
                                                nickname.isNotBlank() &&
                                                altNickname.isNotBlank() &&
                                                username.isNotBlank() &&
                                                realName.isNotBlank()
                                    }
                    ) { Text("Save Only", color = colors.cyan) }
                }
            }
    )
}
