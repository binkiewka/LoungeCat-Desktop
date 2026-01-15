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
import com.loungecat.irc.shared.generated.resources.Res
import com.loungecat.irc.shared.generated.resources.icon
import com.loungecat.irc.shared.generated.resources.logo_transparent
import com.loungecat.irc.ui.screen.DesktopMainScreen
import com.loungecat.irc.ui.theme.LoungeCatTheme
import com.loungecat.irc.ui.theme.ThemeManager
import com.loungecat.irc.util.Logger
import com.loungecat.irc.util.PlatformUtils
import java.awt.SystemTray
import java.io.File
import javax.imageio.ImageIO
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import org.jetbrains.compose.resources.painterResource

fun main() = application {
    // Initialize services
    val appDataDir = File(PlatformUtils.getAppDataDirectory())
    appDataDir.mkdirs()

    DatabaseService.initialize()
    PreferencesManager.initialize(File(PlatformUtils.getConfigPath()))
    ThemeManager.initialize(PlatformUtils.getAppDataDirectory())
    MessageCache.initialize(appDataDir)

    // Load initial preferences
    val initialPrefs = PreferencesManager.getPreferences()

    val connectionManager = remember {
        DesktopConnectionManager().apply {
            setDatabaseService(DatabaseService)

            // Set up message cache callbacks
            messageCacheLoader = { serverId, channelName ->
                MessageCache.loadMessages(serverId, channelName)
            }
            messageCacheSaver = { serverId, channelName, messages ->
                MessageCache.saveMessages(serverId, channelName, messages)
            }
            messageCachePaginatedLoader = { serverId, channelName, offset, limit ->
                MessageCache.loadMessages(serverId, channelName, offset, limit)
            }
            messageCacheCountLoader = { serverId, channelName ->
                MessageCache.getMessageCount(serverId, channelName)
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
                    size = DpSize(1400.dp, 900.dp),
                    position = WindowPosition.Aligned(Alignment.Center)
            )

    // Initialize standard tray state for non-Linux platforms (or fallback)
    val trayState = rememberTrayState()

    // Track if dorkbox successfully loaded
    var isDorkboxReady by remember { mutableStateOf(false) }

    // Linux-specific tray logic using dorkbox/SystemTray
    val scope = rememberCoroutineScope()

    DisposableEffect(Unit) {
        var systemTray: dorkbox.systemTray.SystemTray? = null

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
                            // CRITICAL FIX: Force AppIndicator since libayatana-appindicator3-1 is
                            // installed
                            // Auto-detection seems to hang or fail
                            dorkbox.systemTray.SystemTray.FORCE_TRAY_TYPE =
                                    dorkbox.systemTray.SystemTray.TrayType.AppIndicator

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
                                            classLoader.getResourceAsStream("logo.png")
                                                    ?: classLoader.getResourceAsStream(
                                                            "composeResources/com.loungecat.irc.shared.generated.resources/drawable/logo.png"
                                                    )
                                                            ?: classLoader.getResourceAsStream(
                                                            "drawable/logo.png"
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

    // Standard Tray Fallback
    // Show if NOT Linux, OR if Linux but Dorkbox isn't ready yet (or failed)
    // We use manual AWT SystemTray here to ensure we can resize the image properly
    // and avoid the "black square" issue common with high-res icons on Linux AWT
    if ((!isLinux || !isDorkboxReady) && SystemTray.isSupported()) {
        DisposableEffect(Unit) {
            Logger.d(
                    "Main",
                    "LoungeCat - FALLBACK TRAY ACTIVE (Manual AWT Tray) - isLinux: $isLinux, isDorkboxReady: $isDorkboxReady"
            )
            val tray = SystemTray.getSystemTray()
            var trayIcon: java.awt.TrayIcon? = null

            try {
                val classLoader = Thread.currentThread().contextClassLoader
                val iconStream =
                        classLoader.getResourceAsStream("logo.png")
                                ?: classLoader.getResourceAsStream(
                                        "composeResources/com.loungecat.irc.shared.generated.resources/drawable/logo.png"
                                )
                                        ?: classLoader.getResourceAsStream("drawable/logo.png")
                                        ?: classLoader.getResourceAsStream(
                                        "composeResources/com.loungecat.irc.shared.generated.resources/drawable/logo_transparent.png"
                                )

                if (iconStream != null) {
                    val originalImage = ImageIO.read(iconStream)
                    // Resize to 24x24 for Linux Tray (standard size)
                    // USE TYPE_INT_RGB (No Alpha) to prevent "black square" bug on X11/Cinnamon
                    val resizedImage =
                            java.awt.image.BufferedImage(
                                    24,
                                    24,
                                    java.awt.image.BufferedImage.TYPE_INT_RGB
                            )
                    val g = resizedImage.createGraphics()

                    // Fill background with standard gray (SystemColor.control) or light gray
                    // This replaces transparency with a solid color that renders correctly
                    g.color = java.awt.SystemColor.control
                    g.fillRect(0, 0, 24, 24)

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

                    trayIcon = java.awt.TrayIcon(resizedImage, "LoungeCat (Fallback)", popup)
                    // Disable auto-size if we are manually resizing to 24x24
                    trayIcon.isImageAutoSize = false

                    tray.add(trayIcon)
                    Logger.d("Main", "LoungeCat - Manual AWT Tray added successfully (Opaque/RGB)")
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
            title = "LoungeCat IRC",
            title = "LoungeCat IRC",
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
        LoungeCatTheme(themeColors = themeColors) {
            DesktopMainScreen(connectionManager = connectionManager)
        }
    }
}

// Helper to check if we are running on Linux
private val isLinux = System.getProperty("os.name").lowercase().contains("linux")

class DesktopNotificationService(private val trayState: TrayState) {
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
            trayState.sendNotification(
                    Notification(title = title, message = message, type = Notification.Type.Info)
            )
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
            trayState.sendNotification(
                    Notification(title = title, message = message, type = Notification.Type.Info)
            )
        }
    }
}
