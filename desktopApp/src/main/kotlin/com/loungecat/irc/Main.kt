package com.loungecat.irc

import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.input.key.*
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.*
import com.loungecat.irc.data.PreferencesManager
import com.loungecat.irc.data.cache.MessageCache
import com.loungecat.irc.data.database.DatabaseService
import com.loungecat.irc.service.DesktopConnectionManager
import com.loungecat.irc.service.SoundService
import com.loungecat.irc.service.TextLogService
import com.loungecat.irc.shared.generated.resources.Res
import com.loungecat.irc.shared.generated.resources.icon
import com.loungecat.irc.shared.generated.resources.logo_transparent
import com.loungecat.irc.ui.screen.DesktopMainScreen
import com.loungecat.irc.ui.theme.LoungeCatTheme
import com.loungecat.irc.ui.theme.ThemeManager
import com.loungecat.irc.util.Logger
import com.loungecat.irc.util.PlatformUtils
import java.awt.SystemTray
import java.awt.TrayIcon
import java.io.File
import javax.imageio.ImageIO
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import org.jetbrains.compose.resources.painterResource

fun main() = application {
    // Perform migration if needed (Windows specific)
    PlatformUtils.performWindowsMigration()

    // Initialize services
    val appDataDir = File(System.getProperty("user.home"), ".loungecat")
    if (!appDataDir.exists()) {
        appDataDir.mkdirs()
    }

    DatabaseService.initialize()
    TextLogService.initialize(appDataDir)
    PreferencesManager.initialize(File(PlatformUtils.getConfigPath()))
    ThemeManager.initialize(PlatformUtils.getAppDataDirectory())
    MessageCache.initialize(appDataDir)

    // Load initial permissions
    val initialPrefs = PreferencesManager.getPreferences()

    // Initialize SoundService
    SoundService.initialize(
            soundEnabled = initialPrefs.soundAlertsEnabled,
            soundVolume = initialPrefs.soundVolume
    )
    SoundService.setSoundEnabled(SoundService.SoundType.MENTION, initialPrefs.soundOnMention)
    SoundService.setSoundEnabled(SoundService.SoundType.HIGHLIGHT, initialPrefs.soundOnHighlight)
    SoundService.setSoundEnabled(
            SoundService.SoundType.PRIVATE_MESSAGE,
            initialPrefs.soundOnPrivateMessage
    )

    val connectionManager = remember {
        DesktopConnectionManager().apply {
            setDatabaseService(DatabaseService)

            // Set up message cache callbacks
            messageCacheLoader = { serverId, channelName ->
                MessageCache.loadMessages(serverId, channelName)
            }
            messageCacheSaver = { serverId, channelName, messages, topic ->
                MessageCache.saveMessages(serverId, channelName, messages, topic)
            }
            messageCachePaginatedLoader = { serverId, channelName, offset, limit ->
                MessageCache.loadMessages(serverId, channelName, offset, limit)
            }
            messageCacheCountLoader = { serverId, channelName ->
                MessageCache.getMessageCount(serverId, channelName)
            }
            topicLoader = { serverId, channelName -> MessageCache.getTopic(serverId, channelName) }
            textLogger = { serverId, serverName, channelName, message ->
                TextLogService.logFromMessage(serverId, serverName, channelName, message)
            }

            // Set up preferences updater callback
            setPreferencesUpdater { prefs -> PreferencesManager.updatePreferences { prefs } }

            // Load saved preferences
            loadPreferences(initialPrefs)
        }
    }
    var isVisible by remember { mutableStateOf(true) }

    LaunchedEffect(Unit) {
        connectionManager.loadSavedServers()
        connectionManager.savedConfigs.value.forEach { config ->
            if (config.autoConnect) {
                connectionManager.connect(config, saveToDatabase = false)
            }
        }
    }

    val windowState =
            rememberWindowState(
                    size = DpSize(1280.dp, 800.dp),
                    position = WindowPosition.Aligned(Alignment.Center),
                    placement =
                            if (System.getenv("HEADLESS") == "true") WindowPlacement.Maximized
                            else WindowPlacement.Floating
            )

    // Initialize standard tray state for non-Linux platforms (or fallback)
    val trayState = rememberTrayState()

    // Track if dorkbox successfully loaded
    var isDorkboxReady by remember { mutableStateOf(false) }

    // Linux-specific tray logic using dorkbox/SystemTray
    val scope = rememberCoroutineScope()

    DisposableEffect(Unit) {
        var systemTray: dorkbox.systemTray.SystemTray? = null

        // Skip tray in headless mode
        if (System.getenv("HEADLESS") == "true") {
            return@DisposableEffect onDispose {}
        }

        // Launch initialization in a coroutine to prevent blocking the UI thread
        val job =
                scope.launch(Dispatchers.IO) {
                    if (isLinux) {
                        try {
                            Logger.d(
                                    "Main",
                                    "LoungeCat - Starting dorkbox SystemTray initialization..."
                            )

                            // Advanced configurations
                            dorkbox.systemTray.SystemTray.DEBUG = true
                            dorkbox.systemTray.SystemTray.FORCE_GTK2 = false

                            // Force AppIndicator to try and skip the 3-minute delay of
                            // GtkStatusIcon
                            try {
                                dorkbox.systemTray.SystemTray.FORCE_TRAY_TYPE =
                                        dorkbox.systemTray.SystemTray.TrayType.valueOf(
                                                "AppIndicator"
                                        )

                                // Log available types for debugging (optional but helpful)
                                val trayTypes = dorkbox.systemTray.SystemTray.TrayType.values()
                                Logger.d(
                                        "Main",
                                        "Available TrayTypes: ${trayTypes.joinToString { it.name }}"
                                )

                                // CRITICAL FIX: Force AppIndicator
                                Logger.d("Main", "Forcing TrayType to: AppIndicator (via valueOf)")
                            } catch (e: Exception) {
                                Logger.e("Main", "Could not force AppIndicator: ${e.message}")
                            }

                            // Initialize SystemTray on Background Thread (IO)
                            // We rely on Dorkbox internal threading to handle GTK loops
                            try {
                                Logger.d(
                                        "Main",
                                        "LoungeCat - Running SystemTray.get() on IO thread..."
                                )
                                val tray = dorkbox.systemTray.SystemTray.get()
                                Logger.d("Main", "LoungeCat - SystemTray.get() returned: $tray")

                                if (tray != null) {
                                    systemTray = tray
                                    isDorkboxReady =
                                            true // Signal that we have a native tray (disables
                                    // fallback)

                                    val classLoader = Thread.currentThread().contextClassLoader
                                    val iconStream =
                                            classLoader.getResourceAsStream("tray_icon.png")
                                                    ?: classLoader.getResourceAsStream(
                                                            "composeResources/com.loungecat.irc.shared.generated.resources/drawable/tray_icon.png"
                                                    )
                                                            ?: classLoader.getResourceAsStream(
                                                            "icon.png"
                                                    )
                                                            ?: classLoader.getResourceAsStream(
                                                            "composeResources/com.loungecat.irc.shared.generated.resources/drawable/icon.png"
                                                    )
                                                            ?: classLoader.getResourceAsStream(
                                                            "logo.png"
                                                    )
                                                            ?: classLoader.getResourceAsStream(
                                                            "composeResources/com.loungecat.irc.shared.generated.resources/drawable/logo.png"
                                                    )
                                                            ?: classLoader.getResourceAsStream(
                                                            "composeResources/com.loungecat.irc.shared.generated.resources/drawable/logo_transparent.png"
                                                    )

                                    if (iconStream != null) {
                                        try {
                                            var image = ImageIO.read(iconStream)

                                            // Resize image to standard tray size (24x24) to ensure
                                            // compatibility
                                            // and fix potential "black square" issues if the icon
                                            // is too large/wrong format
                                            val resized =
                                                    java.awt.image.BufferedImage(
                                                            24,
                                                            24,
                                                            java.awt.image.BufferedImage
                                                                    .TYPE_INT_ARGB
                                                    )
                                            val g = resized.createGraphics()
                                            g.drawImage(image, 0, 0, 24, 24, null)
                                            g.dispose()

                                            tray.setImage(resized)
                                            tray.setTooltip("LoungeCat")
                                            Logger.d(
                                                    "Main",
                                                    "LoungeCat - Dorkbox: Tray image set successfully (resized to 24x24)"
                                            )
                                        } catch (e: Exception) {
                                            Logger.e(
                                                    "Main",
                                                    "LoungeCat - Dorkbox: Failed to set tray image: ${e.message}",
                                                    e
                                            )
                                        }
                                    }

                                    val menu = tray.menu
                                    if (menu != null) {
                                        val openItem =
                                                dorkbox.systemTray.MenuItem("Show LoungeCat") {
                                                        action ->
                                                    scope.launch(Dispatchers.Main) {
                                                        isVisible = true
                                                    }
                                                }
                                        menu.add(openItem)

                                        menu.add(dorkbox.systemTray.Separator())

                                        val quitItem =
                                                dorkbox.systemTray.MenuItem("Quit") { action ->
                                                    scope.launch(Dispatchers.Main) {
                                                        connectionManager.shutdown()
                                                        MessageCache.shutdown()
                                                        exitApplication()
                                                    }
                                                }
                                        menu.add(quitItem)
                                    }
                                    Logger.d(
                                            "Main",
                                            "LoungeCat - DORKBOX TRAY ACTIVE and initialized"
                                    )
                                } else {
                                    Logger.e(
                                            "Main",
                                            "LoungeCat - dorkbox SystemTray.get() returned null"
                                    )
                                }
                            } catch (e: Throwable) {
                                Logger.e(
                                        "Main",
                                        "LoungeCat - Error in SystemTray initialization: ${e.message}",
                                        e
                                )
                            }
                        } catch (e: Exception) {
                            Logger.e(
                                    "Main",
                                    "LoungeCat - Error with Dorkbox setup: ${e.message}",
                                    e
                            )
                        }
                    }
                }

        onDispose {
            job.cancel()
            if (isLinux) {
                systemTray?.shutdown()
            }
        }
    }

    val notificationService = remember { DesktopNotificationService(trayState) }

    // Listen for events to trigger sounds and notifications
    LaunchedEffect(connectionManager) {
        launch {
            connectionManager.mentionEvents.collect { (channel, sender, message) ->
                SoundService.play(SoundService.SoundType.MENTION)
                notificationService.showMentionNotification(channel, sender, message)
            }
        }
        launch {
            connectionManager.highlightEvents.collect { (channel, sender, message) ->
                SoundService.play(SoundService.SoundType.HIGHLIGHT)
                notificationService.showMentionNotification(channel, sender, message)
            }
        }
        launch {
            connectionManager.pmEvents.collect { (sender, message) ->
                SoundService.play(SoundService.SoundType.PRIVATE_MESSAGE)
                notificationService.showPrivateMessageNotification(sender, message)
            }
        }
    }

    // Standard Tray Fallback
    // Show if NOT Linux. On Linux, user prefers wait over black box AWT.
    // We use manual AWT SystemTray here to ensure we can resize the image properly
    // and avoid the "black square" issue common with high-res icons on Linux AWT
    if (System.getenv("HEADLESS") != "true" && !isLinux && SystemTray.isSupported()) {
        DisposableEffect(Unit) {
            Logger.d(
                    "Main",
                    "LoungeCat - FALLBACK TRAY ACTIVE (Manual AWT Tray) - isLinux: $isLinux, isDorkboxReady: $isDorkboxReady"
            )
            val tray = SystemTray.getSystemTray()
            var trayIcon: java.awt.TrayIcon? = null

            // Pass the manual tray icon to notification service if available
            notificationService.setManualTray(tray, null)

            try {
                val classLoader = Thread.currentThread().contextClassLoader
                val iconStream =
                        classLoader.getResourceAsStream("tray_icon.png")
                                ?: classLoader.getResourceAsStream(
                                        "composeResources/com.loungecat.irc.shared.generated.resources/drawable/tray_icon.png"
                                )
                                        ?: classLoader.getResourceAsStream("icon.png")
                                        ?: classLoader.getResourceAsStream(
                                        "composeResources/com.loungecat.irc.shared.generated.resources/drawable/icon.png"
                                )
                                        ?: classLoader.getResourceAsStream("logo.png")
                                        ?: classLoader.getResourceAsStream(
                                        "composeResources/com.loungecat.irc.shared.generated.resources/drawable/logo.png"
                                )
                                        ?: classLoader.getResourceAsStream(
                                        "composeResources/com.loungecat.irc.shared.generated.resources/drawable/logo_transparent.png"
                                )

                if (iconStream != null) {
                    val originalImage = ImageIO.read(iconStream)
                    // Resize to 24x24 for Linux Tray (standard size)
                    val resizedImage =
                            java.awt.image.BufferedImage(
                                    24,
                                    24,
                                    java.awt.image.BufferedImage.TYPE_INT_ARGB
                            )
                    val g = resizedImage.createGraphics()
                    g.drawImage(originalImage, 0, 0, 24, 24, null)
                    g.dispose()

                    val popup = java.awt.PopupMenu()
                    val showItem = java.awt.MenuItem("Show LoungeCat")
                    showItem.addActionListener {
                        scope.launch(Dispatchers.Main) { isVisible = true }
                    }
                    val quitItem = java.awt.MenuItem("Quit")
                    quitItem.addActionListener {
                        scope.launch(Dispatchers.Main) {
                            connectionManager.shutdown()
                            MessageCache.shutdown()
                            exitApplication()
                        }
                    }
                    popup.add(showItem)
                    popup.addSeparator()
                    popup.add(quitItem)

                    trayIcon = java.awt.TrayIcon(resizedImage, "LoungeCat", popup)
                    // Auto-size might help if we feed it a larger image, but we are resizing
                    // manually
                    // to be safe.
                    // However, let's keep it false for manual control.
                    trayIcon.isImageAutoSize = false

                    tray.add(trayIcon)
                    notificationService.setManualTray(tray, trayIcon)
                    Logger.d("Main", "LoungeCat - Manual AWT Tray added successfully (Transparent)")
                }
            } catch (e: Exception) {
                Logger.e("Main", "LoungeCat - Fallback tray setup failed: ${e.message}", e)
                e.printStackTrace()
            }

            onDispose {
                if (trayIcon != null) {
                    tray.remove(trayIcon)
                }
            }
        }
    }

    val userPreferences by connectionManager.userPreferences.collectAsState()
    val themeColors =
            remember(userPreferences.theme) { ThemeManager.getTheme(userPreferences.theme) }

    // Update SoundService when preferences change
    LaunchedEffect(userPreferences) {
        SoundService.setEnabled(userPreferences.soundAlertsEnabled)
        SoundService.setVolume(userPreferences.soundVolume)
        SoundService.setSoundEnabled(SoundService.SoundType.MENTION, userPreferences.soundOnMention)
        SoundService.setSoundEnabled(
                SoundService.SoundType.HIGHLIGHT,
                userPreferences.soundOnHighlight
        )
        SoundService.setSoundEnabled(
                SoundService.SoundType.PRIVATE_MESSAGE,
                userPreferences.soundOnPrivateMessage
        )
    }

    Window(
            onCloseRequest = {
                // Check if we have a valid tray to minimize to
                val canMinimizeToTray =
                        if (isLinux) {
                            // On Linux, we can minimize if Dorkbox works OR standard tray works
                            isDorkboxReady || SystemTray.isSupported()
                        } else {
                            SystemTray.isSupported()
                        }

                if (canMinimizeToTray) {
                    isVisible = false
                    notificationService.sendNotification(
                            "LoungeCat",
                            "Minimized to system tray. Right-click to exit."
                    )
                } else {
                    // Safe exit if no tray is available
                    connectionManager.shutdown()
                    MessageCache.shutdown()
                    exitApplication()
                }
            },
            visible = isVisible,
            state = windowState,
            title = "LoungeCat IRC v${com.loungecat.irc.BuildConfig.VERSION}",
            icon = painterResource(Res.drawable.logo_transparent),
            onKeyEvent = { keyEvent ->
                when {
                    keyEvent.isCtrlPressed &&
                            keyEvent.key == Key.Q &&
                            keyEvent.type == KeyEventType.KeyDown -> {
                        connectionManager.shutdown()
                        MessageCache.shutdown()
                        exitApplication()
                        true
                    }
                    keyEvent.isCtrlPressed &&
                            keyEvent.key == Key.H &&
                            keyEvent.type == KeyEventType.KeyDown -> {
                        if (isLinux || SystemTray.isSupported()) isVisible = false
                        true
                    }
                    else -> false
                }
            }
    ) {
        val window = this.window
        DisposableEffect(Unit) {
            if (System.getProperty("os.name").contains("Windows")) {
                window.rootPane.putClientProperty("jetbrains.awt.windowDarkAppearance", true)
            }
            onDispose {}
        }

        LoungeCatTheme(themeColors = themeColors) {
            DesktopMainScreen(connectionManager = connectionManager)
        }
    }
}

// Helper to check if we are running on Linux
private val isLinux = System.getProperty("os.name").lowercase().contains("linux")

class DesktopNotificationService(private val trayState: TrayState) {
    private var manualTray: java.awt.SystemTray? = null
    private var manualTrayIcon: java.awt.TrayIcon? = null

    fun setManualTray(tray: java.awt.SystemTray, icon: java.awt.TrayIcon?) {
        this.manualTray = tray
        this.manualTrayIcon = icon
    }

    private val hasNotifySend: Boolean by lazy {
        try {
            if (isLinux) {
                // Check if notify-send exists
                val process = Runtime.getRuntime().exec(arrayOf("which", "notify-send"))
                process.waitFor() == 0
            } else {
                false
            }
        } catch (e: Exception) {
            false
        }
    }

    fun sendNotification(title: String, message: String) {
        if (hasNotifySend) {
            sendNativeNotification(title, message)
        } else {
            // First try manual AWT tray (Windows/Mac preferred fallback)
            val icon = manualTrayIcon
            if (icon != null) {
                try {
                    icon.displayMessage(title, message, java.awt.TrayIcon.MessageType.INFO)
                } catch (e: Exception) {
                    Logger.e("Main", "Failed to send manual tray notification", e)
                    // Fallback to Compose Tray
                    trayState.sendNotification(
                            Notification(
                                    title = title,
                                    message = message,
                                    type = Notification.Type.Info
                            )
                    )
                }
            } else {
                // Final fallback to Compose Tray
                trayState.sendNotification(
                        Notification(
                                title = title,
                                message = message,
                                type = Notification.Type.Info
                        )
                )
            }
        }
    }

    fun showMentionNotification(channel: String, sender: String, message: String) {
        sendNotification("Mention in $channel", "$sender: $message")
    }

    fun showPrivateMessageNotification(sender: String, message: String) {
        sendNotification("PM from $sender", message)
    }

    private fun sendNativeNotification(title: String, message: String) {
        try {
            // Very basic safety escaping
            val safeTitle = title.replace("\"", "\\\"")
            val safeMessage = message.replace("\"", "\\\"")

            val command =
                    arrayOf(
                            "notify-send",
                            "-i",
                            "utilities-terminal", // Standard icon
                            "-a",
                            "LoungeCat",
                            safeTitle,
                            safeMessage
                    )
            Runtime.getRuntime().exec(command)
        } catch (e: Exception) {
            e.printStackTrace()
            // Fallback
            sendNotification(title, message)
        }
    }
}
