package com.loungecat.irc.ui.theme

import androidx.compose.ui.graphics.Color
import kotlinx.serialization.Serializable

@Serializable
enum class AppTheme {
    DARK,
    LIGHT,
    HIGH_VOLTAGE,
    NEON_NIGHT,
    RETRO_WARMTH,
    STORMY_SEA;

    companion object {
        fun fromString(value: String): AppTheme {
            return entries.find { it.name.equals(value, ignoreCase = true) } ?: DARK
        }
    }
}

data class ThemeColors(
        val isDark: Boolean,
        val background: Color,
        val windowBackground: Color,
        val currentLine: Color,
        val selection: Color,
        val foreground: Color,
        val comment: Color,
        val cyan: Color,
        val green: Color,
        val orange: Color,
        val pink: Color,
        val purple: Color,
        val red: Color,
        val yellow: Color,
        val border: Color,
        val timeColor: Color,
        val msgSelf: Color,
        val msgAction: Color,
        val linkColor: Color,
        val buttonColor: Color,
        val unreadMarker: Color,
        val dateMarker: Color,
        val highlightBg: Color,
        val chatBubbleBg: Color,
        val formBackground: Color,
        val errorColor: Color
)

@Serializable
data class CustomTheme(
        val name: String,
        val isDark: Boolean = true,
        val colors: Map<String, String>
)

object ThemeMapper {
    fun toThemeColors(
            customTheme: CustomTheme,
            fallback: ThemeColors = getThemeColors(AppTheme.DARK)
    ): ThemeColors {
        val c = customTheme.colors
        return ThemeColors(
                isDark = customTheme.isDark,
                background = parseColor(c["background"], fallback.background),
                windowBackground = parseColor(c["windowBackground"], fallback.windowBackground),
                currentLine = parseColor(c["currentLine"], fallback.currentLine),
                selection = parseColor(c["selection"], fallback.selection),
                foreground = parseColor(c["foreground"], fallback.foreground),
                comment = parseColor(c["comment"], fallback.comment),
                cyan = parseColor(c["cyan"], fallback.cyan),
                green = parseColor(c["green"], fallback.green),
                orange = parseColor(c["orange"], fallback.orange),
                pink = parseColor(c["pink"], fallback.pink),
                purple = parseColor(c["purple"], fallback.purple),
                red = parseColor(c["red"], fallback.red),
                yellow = parseColor(c["yellow"], fallback.yellow),
                border = parseColor(c["border"], fallback.border),
                timeColor = parseColor(c["timeColor"], fallback.timeColor),
                msgSelf = parseColor(c["msgSelf"], fallback.msgSelf),
                msgAction = parseColor(c["msgAction"], fallback.msgAction),
                linkColor = parseColor(c["linkColor"], fallback.linkColor),
                buttonColor = parseColor(c["buttonColor"], fallback.buttonColor),
                unreadMarker = parseColor(c["unreadMarker"], fallback.unreadMarker),
                dateMarker = parseColor(c["dateMarker"], fallback.dateMarker),
                highlightBg = parseColor(c["highlightBg"], fallback.highlightBg),
                chatBubbleBg = parseColor(c["chatBubbleBg"], fallback.chatBubbleBg),
                formBackground = parseColor(c["formBackground"], fallback.formBackground),
                errorColor = parseColor(c["errorColor"], fallback.errorColor)
        )
    }

    private fun parseColor(hex: String?, fallback: Color): Color {
        if (hex == null) return fallback
        return try {
            val cleanHex = hex.removePrefix("#")
            when (cleanHex.length) {
                6 -> Color(cleanHex.toLong(16) or 0xFF000000)
                8 -> Color(cleanHex.toLong(16))
                else -> fallback
            }
        } catch (e: Exception) {
            fallback
        }
    }
}

object DarkColors {
    val Background = Color(0xFF141414) // Much darker, near black
    val WindowBackground = Color(0xFF0A0A0A) // Almost pure black
    val CurrentLine = Color(0xFF252525)
    val Selection = Color(0xFF353535)
    val Foreground = Color(0xFFE0E0E0)
    val Comment = Color(0xFF707070)
    val Cyan = Color(0xFF80DEEA)
    val Green = Color(0xFF81C784)
    val Orange = Color(0xFFFFB74D)
    val Pink = Color(0xFFF06292)
    val Purple = Color(0xFF90CAF9)
    val Red = Color(0xFFE57373)
    val Yellow = Color(0xFFFFF176)
}

object LightColors {
    val Background = Color(0xFFFFFFFF)
    val WindowBackground = Color(0xFFF5F5F5)
    val CurrentLine = Color(0xFFE8E8E8)
    val Selection = Color(0xFFCCE0FF)
    val Foreground = Color(0xFF101010)
    val Comment = Color(0xFF606060)
    val Cyan = Color(0xFF006064)
    val Green = Color(0xFF1B5E20)
    val Orange = Color(0xFFE65100)
    val Pink = Color(0xFF880E4F)
    val Purple = Color(0xFF0D47A1)
    val Red = Color(0xFFB71C1C)
    val Yellow = Color(0xFF827717)
}

object HighVoltageColors {
    val Background = Color(0xFF1A1B26)
    val WindowBackground = Color(0xFF16161E)
    val CurrentLine = Color(0xFF292E42)
    val Selection = Color(0xFF3D59A1)
    val Foreground = Color(0xFFFFFFFF)
    val Comment = Color(0xFF565F89)
    val Cyan = Color(0xFF7DCFFF)
    val Green = Color(0xFF73DACA)
    val Orange = Color(0xFFFF9E64)
    val Pink = Color(0xFFFF75A0)
    val Purple = Color(0xFF9AA5CE)
    val Red = Color(0xFFF7768E)
    val Yellow = Color(0xFFE0AF68)
    val Border = Color(0xFF414868)
    val TimeColor = Color(0xFF7AA2F7)
    val LinkColor = Color(0xFF73DACA)
    val DateMarker = Color(0xFFBB9AF7)
    val HighlightBg = Color(0x337AA2F7) // #7AA2F733
    val ChatBubbleBg = Color(0xFF24283B)
    val FormBackground = Color(0xFF1F2335)
}

object NeonNightColors {
    val Background = Color(0xFF090B10)
    val WindowBackground = Color(0xFF050508)
    val CurrentLine = Color(0xFF161925)
    val Selection = Color(0xFF402035)
    val Foreground = Color(0xFFE0F7FA)
    val Comment = Color(0xFF546E7A)
    val Cyan = Color(0xFF00F3FF)
    val Green = Color(0xFF00FF9F)
    val Orange = Color(0xFFFFB86C)
    val Pink = Color(0xFFFF0055)
    val Purple = Color(0xFFBD93F9)
    val Red = Color(0xFFFF5555)
    val Yellow = Color(0xFFF1FA8C)
    val Border = Color(0xFF2E214D)
    val TimeColor = Color(0xFF6272A4)
    val LinkColor = Color(0xFF8BE9FD)
    val HighlightBg = Color(0x33FF0055) // #FF005533
    val ChatBubbleBg = Color(0xFF12141C)
}

object RetroWarmthColors {
    val Background = Color(0xFF32302F)
    val WindowBackground = Color(0xFF282828)
    val CurrentLine = Color(0xFF504945)
    val Selection = Color(0xFF665C54)
    val Foreground = Color(0xFFEBDBB2)
    val Comment = Color(0xFF928374)
    val Cyan = Color(0xFF83A598)
    val Green = Color(0xFFB8BB26)
    val Orange = Color(0xFFFE8019)
    val Pink = Color(0xFFD3869B)
    val Purple = Color(0xFFB16286)
    val Red = Color(0xFFFB4934)
    val Yellow = Color(0xFFFABD2F)
    val LinkColor = Color(0xFF8EC07C)
    val HighlightBg = Color(0x33FABD2F) // #FABD2F33
    val ChatBubbleBg = Color(0xFF3C3836)
    val ErrorColor = Color(0xFFCC241D)
}

object StormySeaColors {
    val Background = Color(0xFF292D3E)
    val WindowBackground = Color(0xFF232635)
    val CurrentLine = Color(0xFF40455B)
    val Selection = Color(0xFF717CB4)
    val Foreground = Color(0xFFEEFFFF)
    val Comment = Color(0xFF676E95)
    val Cyan = Color(0xFF89DDFF)
    val Green = Color(0xFFC3E88D)
    val Orange = Color(0xFFF78C6C)
    val Pink = Color(0xFFFF9CAC)
    val Purple = Color(0xFFC792EA)
    val Red = Color(0xFFFF5370)
    val Yellow = Color(0xFFFFCB6B)
    val Border = Color(0xFF454D6B)
    val TimeColor = Color(0xFF82AAFF)
    val LinkColor = Color(0xFF82AAFF)
    val HighlightBg = Color(0x3389DDFF) // #89DDFF33
    val ChatBubbleBg = Color(0xFF32374D)
    val FormBackground = Color(0xFF2E3244)
}

fun getThemeColors(theme: AppTheme): ThemeColors =
        when (theme) {
            AppTheme.DARK ->
                    ThemeColors(
                            isDark = true,
                            background = DarkColors.Background,
                            windowBackground = DarkColors.WindowBackground,
                            currentLine = DarkColors.CurrentLine,
                            selection = DarkColors.Selection,
                            foreground = DarkColors.Foreground,
                            comment = DarkColors.Comment,
                            cyan = DarkColors.Cyan,
                            green = DarkColors.Green,
                            orange = DarkColors.Orange,
                            pink = DarkColors.Pink,
                            purple = DarkColors.Purple,
                            red = DarkColors.Red,
                            yellow = DarkColors.Yellow,
                            border = DarkColors.CurrentLine,
                            timeColor = DarkColors.Comment,
                            msgSelf = DarkColors.Cyan,
                            msgAction = DarkColors.Yellow,
                            linkColor = DarkColors.Cyan,
                            buttonColor = DarkColors.Green,
                            unreadMarker = DarkColors.Pink,
                            dateMarker = DarkColors.Purple,
                            highlightBg = Color(0x14FFFFFF),
                            chatBubbleBg = Color(0xFF1E1E1E),
                            formBackground = Color(0xFF101010),
                            errorColor = DarkColors.Red
                    )
            AppTheme.LIGHT ->
                    ThemeColors(
                            isDark = false,
                            background = LightColors.Background,
                            windowBackground = LightColors.WindowBackground,
                            currentLine = LightColors.CurrentLine,
                            selection = LightColors.Selection,
                            foreground = LightColors.Foreground,
                            comment = LightColors.Comment,
                            cyan = LightColors.Cyan,
                            green = LightColors.Green,
                            orange = LightColors.Orange,
                            pink = LightColors.Pink,
                            purple = LightColors.Purple,
                            red = LightColors.Red,
                            yellow = LightColors.Yellow,
                            border = Color(0xFFE0E0E0),
                            timeColor = LightColors.Comment,
                            msgSelf = LightColors.Purple,
                            msgAction = LightColors.Orange,
                            linkColor = LightColors.Purple,
                            buttonColor = LightColors.Green,
                            unreadMarker = LightColors.Red,
                            dateMarker = LightColors.Cyan,
                            highlightBg = Color(0x20FBC02D),
                            chatBubbleBg = Color(0xFFF0F0F0),
                            formBackground = Color(0xFFFFFFFF),
                            errorColor = LightColors.Red
                    )
            AppTheme.HIGH_VOLTAGE ->
                    ThemeColors(
                            isDark = true,
                            background = HighVoltageColors.Background,
                            windowBackground = HighVoltageColors.WindowBackground,
                            currentLine = HighVoltageColors.CurrentLine,
                            selection = HighVoltageColors.Selection,
                            foreground = HighVoltageColors.Foreground,
                            comment = HighVoltageColors.Comment,
                            cyan = HighVoltageColors.Cyan,
                            green = HighVoltageColors.Green,
                            orange = HighVoltageColors.Orange,
                            pink = HighVoltageColors.Pink,
                            purple = HighVoltageColors.Purple,
                            red = HighVoltageColors.Red,
                            yellow = HighVoltageColors.Yellow,
                            border = HighVoltageColors.Border,
                            timeColor = HighVoltageColors.TimeColor,
                            msgSelf = HighVoltageColors.Cyan,
                            msgAction = HighVoltageColors.Yellow,
                            linkColor = HighVoltageColors.LinkColor,
                            buttonColor = HighVoltageColors.Pink,
                            unreadMarker = HighVoltageColors.Red,
                            dateMarker = HighVoltageColors.DateMarker,
                            highlightBg = HighVoltageColors.HighlightBg,
                            chatBubbleBg = HighVoltageColors.ChatBubbleBg,
                            formBackground = HighVoltageColors.FormBackground,
                            errorColor = HighVoltageColors.Red
                    )
            AppTheme.NEON_NIGHT ->
                    ThemeColors(
                            isDark = true,
                            background = NeonNightColors.Background,
                            windowBackground = NeonNightColors.WindowBackground,
                            currentLine = NeonNightColors.CurrentLine,
                            selection = NeonNightColors.Selection,
                            foreground = NeonNightColors.Foreground,
                            comment = NeonNightColors.Comment,
                            cyan = NeonNightColors.Cyan,
                            green = NeonNightColors.Green,
                            orange = NeonNightColors.Orange,
                            pink = NeonNightColors.Pink,
                            purple = NeonNightColors.Purple,
                            red = NeonNightColors.Red,
                            yellow = NeonNightColors.Yellow,
                            border = NeonNightColors.Border,
                            timeColor = NeonNightColors.TimeColor,
                            msgSelf = NeonNightColors.Cyan,
                            msgAction = NeonNightColors.Green,
                            linkColor = NeonNightColors.LinkColor,
                            buttonColor = NeonNightColors.Pink,
                            unreadMarker = NeonNightColors.Red,
                            dateMarker = NeonNightColors.Purple,
                            highlightBg = NeonNightColors.HighlightBg,
                            chatBubbleBg = NeonNightColors.ChatBubbleBg,
                            formBackground = NeonNightColors.ChatBubbleBg,
                            errorColor = NeonNightColors.Red
                    )
            AppTheme.RETRO_WARMTH ->
                    ThemeColors(
                            isDark = true,
                            background = RetroWarmthColors.Background,
                            windowBackground = RetroWarmthColors.WindowBackground,
                            currentLine = RetroWarmthColors.CurrentLine,
                            selection = RetroWarmthColors.Selection,
                            foreground = RetroWarmthColors.Foreground,
                            comment = RetroWarmthColors.Comment,
                            cyan = RetroWarmthColors.Cyan,
                            green = RetroWarmthColors.Green,
                            orange = RetroWarmthColors.Orange,
                            pink = RetroWarmthColors.Pink,
                            purple = RetroWarmthColors.Purple,
                            red = RetroWarmthColors.Red,
                            yellow = RetroWarmthColors.Yellow,
                            border = RetroWarmthColors.CurrentLine,
                            timeColor = RetroWarmthColors.Cyan,
                            msgSelf = RetroWarmthColors.Green,
                            msgAction = RetroWarmthColors.Orange,
                            linkColor = RetroWarmthColors.LinkColor,
                            buttonColor = RetroWarmthColors.Pink,
                            unreadMarker = RetroWarmthColors.Red,
                            dateMarker = RetroWarmthColors.Pink,
                            highlightBg = RetroWarmthColors.HighlightBg,
                            chatBubbleBg = RetroWarmthColors.ChatBubbleBg,
                            formBackground = RetroWarmthColors.ChatBubbleBg,
                            errorColor = RetroWarmthColors.ErrorColor
                    )
            AppTheme.STORMY_SEA ->
                    ThemeColors(
                            isDark = true,
                            background = StormySeaColors.Background,
                            windowBackground = StormySeaColors.WindowBackground,
                            currentLine = StormySeaColors.CurrentLine,
                            selection = StormySeaColors.Selection,
                            foreground = StormySeaColors.Foreground,
                            comment = StormySeaColors.Comment,
                            cyan = StormySeaColors.Cyan,
                            green = StormySeaColors.Green,
                            orange = StormySeaColors.Orange,
                            pink = StormySeaColors.Pink,
                            purple = StormySeaColors.Purple,
                            red = StormySeaColors.Red,
                            yellow = StormySeaColors.Yellow,
                            border = StormySeaColors.Border,
                            timeColor = StormySeaColors.TimeColor,
                            msgSelf = StormySeaColors.Cyan,
                            msgAction = StormySeaColors.Yellow,
                            linkColor = StormySeaColors.LinkColor,
                            buttonColor = StormySeaColors.Purple,
                            unreadMarker = StormySeaColors.Red,
                            dateMarker = StormySeaColors.Purple,
                            highlightBg = StormySeaColors.HighlightBg,
                            chatBubbleBg = StormySeaColors.ChatBubbleBg,
                            formBackground = StormySeaColors.FormBackground,
                            errorColor = StormySeaColors.Red
                    )
        }
