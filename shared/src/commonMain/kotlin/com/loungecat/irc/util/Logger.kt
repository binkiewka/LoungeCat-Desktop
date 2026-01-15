package com.loungecat.irc.util

expect fun platformLog(tag: String, message: String)
expect fun platformLogError(tag: String, message: String, throwable: Throwable? = null)

object Logger {
    fun d(tag: String, message: String) = platformLog(tag, message)
    fun debug(tag: String, message: String) = platformLog(tag, message)
    fun i(tag: String, message: String) = platformLog(tag, message)
    fun w(tag: String, message: String) = platformLog(tag, "[WARN] $message")
    fun warn(tag: String, message: String) = platformLog(tag, "[WARN] $message")
    fun e(tag: String, message: String, throwable: Throwable? = null) = platformLogError(tag, message, throwable)
    fun error(tag: String, message: String, throwable: Throwable? = null) = platformLogError(tag, message, throwable)
}
