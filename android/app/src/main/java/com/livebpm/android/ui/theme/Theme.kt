package com.livebpm.android.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

private val LiveBpmColors = darkColorScheme(
    primary = AccentCyan,
    onPrimary = BgDeep,
    secondary = AccentMagenta,
    onSecondary = BgDeep,
    tertiary = AccentViolet,
    background = BgDeep,
    onBackground = TextPrimary,
    surface = SurfacePanel,
    onSurface = TextPrimary,
    surfaceVariant = SurfaceElevated,
    onSurfaceVariant = TextMuted,
    outline = BorderColor,
    error = Danger,
    onError = BgDeep,
)

@Composable
fun LiveBpmTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = LiveBpmColors,
        typography = LiveBpmTypography,
        content = content,
    )
}
