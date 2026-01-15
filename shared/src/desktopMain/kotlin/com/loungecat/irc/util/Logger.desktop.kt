package com.loungecat.irc.util

import org.slf4j.LoggerFactory

private val logger = LoggerFactory.getLogger("LoungeCat")

actual fun platformLog(tag: String, message: String) {
    logger.info("[$tag] $message")
}

actual fun platformLogError(tag: String, message: String, throwable: Throwable?) {
    if (throwable != null) {
        logger.error("[$tag] $message", throwable)
    } else {
        logger.error("[$tag] $message")
    }
}
