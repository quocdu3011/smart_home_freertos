package com.quocdu.smarthomemqtt.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable

private val LightColors = lightColorScheme(
    primary = DeepSea,
    onPrimary = Cloud,
    secondary = Clay,
    onSecondary = Cloud,
    tertiary = SunAccent,
    background = AppSand,
    onBackground = DeepSea,
    surface = Cloud,
    onSurface = DeepSea
)

private val DarkColors = darkColorScheme(
    primary = MintMist,
    onPrimary = DeepSea,
    secondary = SunAccent,
    onSecondary = DeepSea,
    tertiary = Clay,
    background = DeepSea,
    onBackground = Cloud,
    surface = ColorTokens.darkSurface,
    onSurface = Cloud
)

@Composable
fun SmartHomeMqttTheme(
    content: @Composable () -> Unit
) {
    MaterialTheme(
        colorScheme = LightColors,
        typography = AppTypography,
        content = content
    )
}

private object ColorTokens {
    val darkSurface = DeepSea.copy(alpha = 0.88f)
}
