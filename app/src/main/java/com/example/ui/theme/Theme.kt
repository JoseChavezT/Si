package com.example.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable

private val DarkColorScheme = darkColorScheme(
  primary = GreenPrimary,
  secondary = GreenDark,
  tertiary = GraySecondary,
  background = AmoledBackground,
  surface = AmoledSurface,
  onPrimary = AmoledBackground,
  onSecondary = TextWhite,
  onTertiary = TextWhite,
  onBackground = TextWhite,
  onSurface = TextWhite,
  onSurfaceVariant = TextSecondary,
  outline = SubtleBorder
)

@Composable
fun MyApplicationTheme(
  content: @Composable () -> Unit,
) {
  MaterialTheme(
    colorScheme = DarkColorScheme,
    typography = Typography,
    content = content
  )
}
