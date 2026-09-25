package app.speecher.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp

// Flat sketches of the two system screens in the restricted-settings steps, in the manner of
// Android's setup illustrations and ButtonLayoutPicker's panel sketch: palette colours, placeholder
// bars for text that doesn't matter, and real words only where the user has to find them.

/** Accessibility settings: a list with Speecher chip greyed out as restricted, and a tap on it. */
@Composable
internal fun BlockedRowIllustration(modifier: Modifier = Modifier) {
    val colors = MaterialTheme.colorScheme
    val text = rememberTextMeasurer()
    Canvas(modifier.padding(vertical = 4.dp).size(ILLUSTRATION_WIDTH, 84.dp)) {
        val unit = size.height / 21
        drawRoundRect(colors.surfaceContainerHighest, cornerRadius = CornerRadius(unit * 2))
        val placeholder = colors.onSurfaceVariant.copy(alpha = 0.4f)
        val rowHeight = unit * 6
        listOf(0.5f, null, 0.4f).forEachIndexed { index, width ->
            val top = unit * 1.5f + rowHeight * index
            val iconCenter = Offset(unit * 4, top + rowHeight / 2)
            if (width != null) {
                drawCircle(placeholder, unit * 1.5f, iconCenter)
                drawBar(placeholder, Offset(unit * 7, top + rowHeight / 2 - unit * 0.6f), width)
                return@forEachIndexed
            }
            // The restricted row: the system draws it disabled, so everything in it is faded.
            val faded = colors.onSurface.copy(alpha = 0.38f)
            drawCircle(faded, unit * 1.5f, iconCenter)
            drawLabel(text, "Speecher chip", Offset(unit * 7, top + unit * 0.6f), faded, unit * 2)
            drawLabel(
                text,
                "Restricted setting",
                Offset(unit * 7, top + unit * 3.1f),
                faded,
                unit * 1.6f,
            )
            drawTap(colors.primary, Offset(size.width * 0.72f, top + rowHeight / 2), unit)
        }
    }
}

/**
 * App info: the three-dot menu open with Allow restricted settings highlighted and tapped. The menu
 * hangs from the dots in the top corner, as the system draws it.
 */
@Composable
internal fun AllowRestrictedIllustration(modifier: Modifier = Modifier) {
    val colors = MaterialTheme.colorScheme
    val text = rememberTextMeasurer()
    Canvas(modifier.padding(vertical = 4.dp).size(ILLUSTRATION_WIDTH, 84.dp)) {
        val unit = size.height / 21
        drawRoundRect(colors.surfaceContainerHighest, cornerRadius = CornerRadius(unit * 2))
        val placeholder = colors.onSurfaceVariant.copy(alpha = 0.4f)
        // Header: the app's icon and name, with the menu's three dots in the corner.
        drawCircle(colors.inverseSurface, unit * 2.2f, Offset(unit * 4.5f, unit * 5))
        drawBar(placeholder, Offset(unit * 8, unit * 4.4f), 0.22f)
        drawBar(placeholder, Offset(unit * 2.5f, unit * 11), 0.3f)
        drawBar(placeholder, Offset(unit * 2.5f, unit * 15), 0.24f)
        val dotsX = size.width - unit * 2.5f
        repeat(3) {
            drawCircle(
                colors.onSurfaceVariant,
                unit * 0.45f,
                Offset(dotsX, unit * (2.8f + it * 1.4f)),
            )
        }
        // The open menu, with its second item highlighted.
        val menuLeft = size.width * 0.3f
        val menuTop = unit * 2
        val menuSize = Size(size.width - menuLeft - unit * 4, unit * 12)
        drawRoundRect(colors.surface, Offset(menuLeft, menuTop), menuSize, CornerRadius(unit))
        drawRoundRect(
            colors.outlineVariant,
            Offset(menuLeft, menuTop),
            menuSize,
            CornerRadius(unit),
            style = Stroke(unit * 0.15f),
        )
        drawBar(placeholder, Offset(menuLeft + unit * 1.5f, menuTop + unit * 2.4f), 0.2f)
        val itemTop = menuTop + unit * 5
        drawRoundRect(
            colors.secondaryContainer,
            Offset(menuLeft + unit * 0.5f, itemTop),
            Size(menuSize.width - unit, unit * 6),
            CornerRadius(unit * 0.8f),
        )
        drawLabel(
            text,
            "Allow restricted settings",
            Offset(menuLeft + unit * 1.5f, itemTop + unit * 1.6f),
            colors.onSecondaryContainer,
            unit * 1.9f,
        )
        // At the item's end, clear of its label.
        drawTap(
            colors.primary,
            Offset(menuLeft + menuSize.width - unit * 2.8f, itemTop + unit * 3),
            unit,
        )
    }
}

private val ILLUSTRATION_WIDTH = 200.dp

/** A rounded placeholder line [fraction] of the illustration's width long. */
private fun DrawScope.drawBar(color: Color, topLeft: Offset, fraction: Float) {
    val height = size.height / 21 * 1.2f
    drawRoundRect(color, topLeft, Size(size.width * fraction, height), CornerRadius(height / 2))
}

private fun DrawScope.drawLabel(
    measurer: TextMeasurer,
    label: String,
    topLeft: Offset,
    color: Color,
    height: Float,
) {
    drawText(measurer, label, topLeft, TextStyle(color = color, fontSize = height.toSp()))
}

/** The usual touch marker: a filled dot inside a soft ring. */
private fun DrawScope.drawTap(color: Color, center: Offset, unit: Float) {
    drawCircle(color.copy(alpha = 0.25f), unit * 2.6f, center)
    drawCircle(color, unit * 1.2f, center)
}

@PreviewLightDark
@Composable
internal fun RestrictedSettingsIllustrationsPreview() = SpeecherTheme {
    Surface {
        Column(Modifier.width(240.dp).padding(16.dp)) {
            BlockedRowIllustration()
            AllowRestrictedIllustration()
        }
    }
}
