package app.speecher.android.ui

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalInspectionMode

// The fallback schemes, used only where there is no wallpaper to draw dynamic colour from
// (previews).
// They keep the desktop mark's warm off-white (#F2F0E6) and near-black (#1F1F1F) as the primary
// pair
// and fill everything else with warm greys between them, so the only other colour is the error red.

private val Light =
    lightColorScheme(
        primary = Color(0xFF1F1F1F),
        onPrimary = Color(0xFFF2F0E6),
        primaryContainer = Color(0xFFE6E3D8),
        onPrimaryContainer = Color(0xFF1F1F1F),
        inversePrimary = Color(0xFFF2F0E6),
        secondary = Color(0xFF5F5C53),
        onSecondary = Color(0xFFFFFFFF),
        secondaryContainer = Color(0xFFE6E3D8),
        onSecondaryContainer = Color(0xFF1F1F1F),
        tertiary = Color(0xFF5F5C53),
        onTertiary = Color(0xFFFFFFFF),
        tertiaryContainer = Color(0xFFE6E3D8),
        onTertiaryContainer = Color(0xFF1F1F1F),
        background = Color(0xFFFAF9F4),
        onBackground = Color(0xFF1C1B18),
        surface = Color(0xFFFAF9F4),
        onSurface = Color(0xFF1C1B18),
        surfaceVariant = Color(0xFFE6E3D8),
        onSurfaceVariant = Color(0xFF5F5C53),
        surfaceTint = Color(0xFF1F1F1F),
        inverseSurface = Color(0xFF31302C),
        inverseOnSurface = Color(0xFFF2F0E6),
        error = Color(0xFFB3261E),
        onError = Color(0xFFFFFFFF),
        errorContainer = Color(0xFFF9DEDC),
        onErrorContainer = Color(0xFF410E0B),
        outline = Color(0xFF8C897F),
        outlineVariant = Color(0xFFD6D3C7),
        surfaceBright = Color(0xFFFAF9F4),
        surfaceDim = Color(0xFFDAD8CF),
        surfaceContainerLowest = Color(0xFFFFFFFF),
        surfaceContainerLow = Color(0xFFF5F3EC),
        surfaceContainer = Color(0xFFEFEDE5),
        surfaceContainerHigh = Color(0xFFE9E7DE),
        surfaceContainerHighest = Color(0xFFE3E1D7),
    )

private val Dark =
    darkColorScheme(
        primary = Color(0xFFF2F0E6),
        onPrimary = Color(0xFF1F1F1F),
        primaryContainer = Color(0xFF3A3935),
        onPrimaryContainer = Color(0xFFF2F0E6),
        inversePrimary = Color(0xFF1F1F1F),
        secondary = Color(0xFFC9C6BA),
        onSecondary = Color(0xFF31302C),
        secondaryContainer = Color(0xFF3A3935),
        onSecondaryContainer = Color(0xFFE6E3D8),
        tertiary = Color(0xFFC9C6BA),
        onTertiary = Color(0xFF31302C),
        tertiaryContainer = Color(0xFF3A3935),
        onTertiaryContainer = Color(0xFFE6E3D8),
        background = Color(0xFF131312),
        onBackground = Color(0xFFE6E4DA),
        surface = Color(0xFF131312),
        onSurface = Color(0xFFE6E4DA),
        surfaceVariant = Color(0xFF3A3935),
        onSurfaceVariant = Color(0xFFBAB7AB),
        surfaceTint = Color(0xFFF2F0E6),
        inverseSurface = Color(0xFFE6E4DA),
        inverseOnSurface = Color(0xFF31302C),
        error = Color(0xFFF2B8B5),
        onError = Color(0xFF601410),
        errorContainer = Color(0xFF8C1D18),
        onErrorContainer = Color(0xFFF9DEDC),
        outline = Color(0xFF8C897F),
        outlineVariant = Color(0xFF45433E),
        surfaceBright = Color(0xFF3A3936),
        surfaceDim = Color(0xFF131312),
        surfaceContainerLowest = Color(0xFF0E0E0D),
        surfaceContainerLow = Color(0xFF1B1B19),
        surfaceContainer = Color(0xFF1F1F1D),
        surfaceContainerHigh = Color(0xFF2A2A27),
        surfaceContainerHighest = Color(0xFF353532),
    )

/**
 * Speecher's Material 3 theme, in the wallpaper's dynamic colours (minSdk 31 always has them).
 * Previews have no wallpaper, so they keep the ink-and-bone schemes.
 */
@Composable
fun SpeecherTheme(content: @Composable () -> Unit) {
    val dark = isSystemInDarkTheme()
    val context = LocalContext.current
    val colors =
        when {
            LocalInspectionMode.current -> if (dark) Dark else Light
            dark -> dynamicDarkColorScheme(context)
            else -> dynamicLightColorScheme(context)
        }
    MaterialTheme(colorScheme = colors, content = content)
}
