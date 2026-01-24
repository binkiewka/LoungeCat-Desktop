package com.loungecat.irc.util

expect fun openUrl(url: String)

expect fun getSystemInfo(
        hideOs: Boolean = false,
        hideCpu: Boolean = false,
        hideMemory: Boolean = false,
        hideStorage: Boolean = false,
        hideVga: Boolean = false,
        hideUptime: Boolean = false
): String
