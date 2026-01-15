package com.loungecat.irc.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.*
import androidx.compose.ui.graphics.Color

val LocalThemeColors = staticCompositionLocalOf { getThemeColors(AppTheme.DARK) }

object AppColors {
        val current: ThemeColors
                @Composable get() = LocalThemeColors.current
}

@Composable
fun LoungeCatTheme(
        theme: AppTheme = AppTheme.DARK,
        themeColors: ThemeColors? = null, // Allow explicit colors
        content: @Composable () -> Unit
) {
        val actualColors = themeColors ?: getThemeColors(theme)

        val colorScheme =
                darkColorScheme(
                        primary = actualColors.green,
                        onPrimary = actualColors.windowBackground,
                        secondary = actualColors.purple,
                        onSecondary = actualColors.foreground,
                        tertiary = actualColors.pink,
                        background = actualColors.background,
                        onBackground = actualColors.foreground,
                        surface = actualColors.windowBackground,
                        onSurface = actualColors.foreground,
                        surfaceVariant = actualColors.windowBackground,
                        onSurfaceVariant = actualColors.comment,
                        error = actualColors.red,
                        onError = Color.White,
                        outline = actualColors.border,
                        outlineVariant = actualColors.comment
                )

        CompositionLocalProvider(LocalThemeColors provides actualColors) {
                MaterialTheme(colorScheme = colorScheme, content = content)
        }
}
