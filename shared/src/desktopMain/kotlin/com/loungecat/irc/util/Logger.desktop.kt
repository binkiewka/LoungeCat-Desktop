package com.loungecat.irc.util

import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.time.LocalDateTime
import org.slf4j.LoggerFactory

private val logger = LoggerFactory.getLogger("LoungeCat")

private val debugLogFile by lazy {
    try {
        val home = System.getProperty("user.home")
        // Try Desktop first, fall back to user home if needed
        val desktop = File(home, "Desktop")
        val targetDir = if (desktop.exists() && desktop.isDirectory) desktop else File(home)

        val file = File(targetDir, "LoungeCat_Debug.log")
        file.appendText("\n\n--- START LOGGING at ${LocalDateTime.now()} ---\n")
        file
    } catch (e: Exception) {
        null
    }
}

private fun logToFile(tag: String, message: String, throwable: Throwable? = null) {
    try {
        debugLogFile?.let { file ->
            val timestamp = LocalDateTime.now()
            file.appendText("[$timestamp] [$tag] $message\n")
            throwable?.let { t ->
                val sw = StringWriter()
                val pw = PrintWriter(sw)
                t.printStackTrace(pw)
                file.appendText(sw.toString() + "\n")
            }
        }
    } catch (e: Exception) {
        // Ignore logging errors to prevent loops
    }
}

actual fun platformLog(tag: String, message: String) {
    logger.info("[$tag] $message")
    logToFile(tag, message)
}

actual fun platformLogError(tag: String, message: String, throwable: Throwable?) {
    if (throwable != null) {
        logger.error("[$tag] $message", throwable)
        logToFile(tag, message, throwable)
    } else {
        logger.error("[$tag] $message")
        logToFile(tag, message)
    }
}
