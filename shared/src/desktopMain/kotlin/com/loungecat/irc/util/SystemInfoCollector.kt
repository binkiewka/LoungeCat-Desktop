package com.loungecat.irc.util

import com.loungecat.irc.BuildConfig
import java.io.BufferedReader
import java.io.File
import java.io.InputStreamReader
import java.lang.management.ManagementFactory
import java.util.Locale
import java.util.concurrent.TimeUnit

object SystemInfoCollector {

    data class SysInfoData(
            val client: String,
            val os: String,
            val cpu: String,
            val memory: String,
            val storage: String,
            val vga: String,
            val uptime: String
    )

    fun getSystemInfo(
            hideOs: Boolean = false,
            hideCpu: Boolean = false,
            hideMemory: Boolean = false,
            hideStorage: Boolean = false,
            hideVga: Boolean = false,
            hideUptime: Boolean = false
    ): String {

        val data = collectData()
        val parts = mutableListOf<String>()

        parts.add("Client: ${data.client}")

        if (!hideOs) parts.add("OS: ${data.os}")
        if (!hideCpu) parts.add("CPU: ${data.cpu}")
        if (!hideMemory) parts.add("Memory: ${data.memory}")
        if (!hideStorage) parts.add("Storage: ${data.storage}")
        if (!hideVga && data.vga.isNotBlank()) parts.add("VGA: ${data.vga}")
        if (!hideUptime) parts.add("Uptime: ${data.uptime}")

        return parts.joinToString(" • ")
    }

    fun getOsInfo(): String = collectData().os
    fun getCpuInfo(): String = collectData().cpu
    fun getMemoryInfo(): String = collectData().memory
    fun getStorageInfo(): String = collectData().storage
    fun getDisplayInfo(): String = collectData().vga
    fun getUptimeInfo(): String = collectData().uptime

    private fun collectData(): SysInfoData {
        val osName = System.getProperty("os.name")
        val isLinux = osName.lowercase().contains("linux")
        val isWindows = osName.lowercase().contains("win")
        val isMac = osName.lowercase().contains("mac")

        // Client
        val client = "LoungeCat ${BuildConfig.VERSION}"

        // OS
        val osVersion = System.getProperty("os.version")
        val osArch = System.getProperty("os.arch")
        val osFull = "$osName $osVersion ($osArch)"

        // CPU
        var cpu = "Unknown"
        try {
            cpu =
                    when {
                        isLinux -> getLinuxCpu()
                        isWindows -> getWindowsCpu()
                        isMac -> getMacCpu()
                        else -> "Generic $osArch Processor"
                    }
        } catch (e: Exception) {
            Logger.e("SysInfo", "Failed to get CPU info", e)
        }

        // Memory
        val memory =
                try {
                    if (isLinux) getLinuxMemory() else getGenericMemory()
                } catch (e: Exception) {
                    "Unknown"
                }

        // Storage
        val storage =
                try {
                    val roots = File.listRoots()
                    var total = 0L
                    var free = 0L
                    roots?.forEach {
                        total += it.totalSpace
                        free += it.usableSpace
                    }
                    formatStorage(total, free)
                } catch (e: Exception) {
                    "Unknown"
                }

        // VGA
        val vga =
                try {
                    when {
                        isLinux -> getLinuxGpu()
                        isWindows -> getWindowsGpu() // Tough without external deps usually
                        else -> ""
                    }
                } catch (e: Exception) {
                    ""
                }

        // Uptime
        val uptime =
                try {
                    val uptimeMillis = ManagementFactory.getRuntimeMXBean().uptime
                    formatUptime(uptimeMillis)
                } catch (e: Exception) {
                    "Unknown"
                }

        return SysInfoData(client, osFull, cpu, memory, storage, vga, uptime)
    }

    private fun getLinuxCpu(): String {
        return try {
            val process =
                    Runtime.getRuntime()
                            .exec(
                                    arrayOf(
                                            "/bin/sh",
                                            "-c",
                                            "cat /proc/cpuinfo | grep 'model name' | head -n 1"
                                    )
                            )
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            val line = reader.readLine()
            process.waitFor(2, TimeUnit.SECONDS) // Add timeout
            if (line != null && line.isNotBlank()) {
                line.substringAfter(":")?.trim() ?: getCpuFallback()
            } else {
                getCpuFallback()
            }
        } catch (e: Exception) {
            getCpuFallback()
        }
    }

    private fun getCpuFallback(): String {
        val arch = System.getProperty("os.arch")
        val cores = Runtime.getRuntime().availableProcessors()
        return "Generic $arch Processor ($cores cores)"
    }

    private fun getWindowsCpu(): String {
        return try {
            val process = Runtime.getRuntime().exec("wmic cpu get name")
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            reader.readLine() // Header
            val line = reader.readLine() // Value
            process.waitFor()
            line?.trim() ?: "Unknown Windows CPU"
        } catch (e: Exception) {
            "Unknown Windows CPU"
        }
    }

    private fun getMacCpu(): String {
        return try {
            val process =
                    Runtime.getRuntime().exec(arrayOf("sysctl", "-n", "machdep.cpu.brand_string"))
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            val line = reader.readLine()
            process.waitFor()
            line?.trim() ?: "Unknown Mac CPU"
        } catch (e: Exception) {
            "Unknown Mac CPU"
        }
    }

    private fun getLinuxMemory(): String {
        var total = 0L
        var available = 0L
        var found = false

        try {
            File("/proc/meminfo").forEachLine { line ->
                if (line.startsWith("MemTotal:")) {
                    total = parseMemInfo(line)
                    found = true
                } else if (line.startsWith("MemAvailable:")) {
                    available = parseMemInfo(line)
                }
            }
        } catch (e: Exception) {
            found = false
        }

        if (found && total > 0) {
            return formatMemory(total, available)
        } else {
            return getGenericMemory()
        }
    }

    private fun parseMemInfo(line: String): Long {
        // format: "MemTotal:       32780228 kB"
        val parts = line.split("\\s+".toRegex())
        if (parts.size >= 2) {
            return parts[1].toLongOrNull()?.times(1024) ?: 0L
        }
        return 0L
    }

    private fun getGenericMemory(): String {
        val bean =
                ManagementFactory.getOperatingSystemMXBean() as?
                        com.sun.management.OperatingSystemMXBean
        if (bean != null) {
            val total = bean.totalMemorySize
            val free = bean.freeMemorySize
            return formatMemory(total, free)
        }
        return "Unknown"
    }

    private fun getLinuxGpu(): String {
        return try {
            val process =
                    Runtime.getRuntime()
                            .exec(arrayOf("/bin/sh", "-c", "lspci | grep VGA | head -n 1"))
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            val line = reader.readLine()
            process.waitFor(2, TimeUnit.SECONDS)
            if (line != null && line.isNotBlank()) {
                val parts = line.split(":")
                if (parts.size >= 3) {
                    return line.substringAfter("controller:").trim()
                }
                return line.substringAfter("VGA compatible controller:").trim()
            }
            "Unknown (Sandboxed)"
        } catch (e: Exception) {
            "Unknown (Sandboxed)"
        }
    }

    private fun getWindowsGpu(): String {
        // "wmic path win32_VideoController get name"
        return try {
            val process = Runtime.getRuntime().exec("wmic path win32_VideoController get name")
            val reader = BufferedReader(InputStreamReader(process.inputStream))
            reader.readLine() // Header
            val line = reader.readLine() // Value
            process.waitFor()
            line?.trim() ?: ""
        } catch (e: Exception) {
            ""
        }
    }

    private fun formatMemory(totalBytes: Long, freeBytes: Long): String {
        val totalGiB = totalBytes / (1024.0 * 1024.0 * 1024.0)
        val freeGiB = freeBytes / (1024.0 * 1024.0 * 1024.0)
        return "%.1f GiB Total (%.1f GiB Free)".format(Locale.ROOT, totalGiB, freeGiB)
    }

    private fun formatStorage(totalBytes: Long, freeBytes: Long): String {
        val totalGiB = totalBytes / (1024.0 * 1024.0 * 1024.0)
        val freeGiB = freeBytes / (1024.0 * 1024.0 * 1024.0)
        // HexChat format: 197.9 GiB / 229.4 GiB (31.5 GiB Free)
        // Used / Total (Free)
        val usedGiB = totalGiB - freeGiB
        return "%.1f GiB / %.1f GiB (%.1f GiB Free)".format(Locale.ROOT, usedGiB, totalGiB, freeGiB)
    }

    private fun formatUptime(millis: Long): String {
        val days = TimeUnit.MILLISECONDS.toDays(millis)
        val hours = TimeUnit.MILLISECONDS.toHours(millis) % 24
        val minutes = TimeUnit.MILLISECONDS.toMinutes(millis) % 60
        val seconds = TimeUnit.MILLISECONDS.toSeconds(millis) % 60
        return "${days}d ${hours}h ${minutes}m ${seconds}s"
    }
}
