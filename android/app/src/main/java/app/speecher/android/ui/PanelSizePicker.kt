package app.speecher.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.size
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.dictation.PanelSize

private val PanelSize.label: String
    get() =
        when (this) {
            PanelSize.Full -> "Full"
            PanelSize.Compact -> "Compact"
            PanelSize.Minimized -> "Minimized"
        }

/** One radio row per [PanelSize], each with a sketch of how much of the screen the panel takes. */
@Composable
fun PanelSizePicker(selected: PanelSize, onSelect: (PanelSize) -> Unit) {
    IllustratedPicker(PanelSize.entries, selected, onSelect, { it.label }) { size, modifier ->
        PanelSizeIllustration(size, modifier)
    }
}

/**
 * A flat sketch of a phone screen: the app's lines at the top and the panel along the bottom at
 * [size]'s share of the height, with the waveform, transcript and Insert it has room for.
 */
@Composable
private fun PanelSizeIllustration(size: PanelSize, modifier: Modifier = Modifier) {
    val colors = MaterialTheme.colorScheme
    Canvas(modifier.size(36.dp, 64.dp)) {
        val unit = this.size.height / 32
        val width = this.size.width
        drawRoundRect(colors.surfaceContainerHighest, cornerRadius = CornerRadius(unit * 2))
        val line = colors.onSurfaceVariant.copy(alpha = 0.4f)
        fun bar(left: Float, top: Float, length: Float) =
            drawRoundRect(line, Offset(left, top), Size(length, unit), CornerRadius(unit / 2))
        listOf(0.7f, 0.5f).forEachIndexed { index, fraction ->
            bar(unit * 2, unit * (2.5f + index * 2.5f), (width - unit * 4) * fraction)
        }
        val panelHeight =
            when (size) {
                PanelSize.Full -> unit * 14
                PanelSize.Compact -> unit * 10
                PanelSize.Minimized -> unit * 4
            }
        val top = this.size.height - panelHeight
        drawRoundRect(
            colors.surfaceContainerLowest,
            Offset(0f, top),
            Size(width, panelHeight),
            CornerRadius(unit * 2),
        )
        val insertWidth = unit * 6
        val insertHeight = unit * 2.5f
        val insertLeft = width - unit * 1.5f - insertWidth
        if (size == PanelSize.Minimized) {
            val middle = top + panelHeight / 2
            drawCircle(colors.error, unit * 0.8f, Offset(unit * 2, middle))
            bar(unit * 3.5f, middle - unit / 2, insertLeft - unit * 4.5f)
            drawRoundRect(
                colors.primary,
                Offset(insertLeft, middle - insertHeight / 2),
                Size(insertWidth, insertHeight),
                CornerRadius(insertHeight / 2),
            )
            return@Canvas
        }
        // A short waveform, then as many transcript lines as the size shows.
        val waveHeights = listOf(1f, 2f, 3f, 2f, 1f)
        waveHeights.forEachIndexed { index, height ->
            val barHeight = unit * height
            drawRoundRect(
                colors.onSurface,
                Offset(width / 2 + unit * (index - 2.5f) * 1.6f, top + unit * 2.5f - barHeight / 2),
                Size(unit, barHeight),
                CornerRadius(unit / 2),
            )
        }
        val lines = if (size == PanelSize.Full) listOf(0.8f, 0.6f, 0.7f) else listOf(0.7f)
        lines.forEachIndexed { index, fraction ->
            bar(unit * 1.5f, top + unit * (4.5f + index * 2f), (width - unit * 3) * fraction)
        }
        drawRoundRect(
            colors.primary,
            Offset(insertLeft, this.size.height - unit * 1.5f - insertHeight),
            Size(insertWidth, insertHeight),
            CornerRadius(insertHeight / 2),
        )
    }
}

@PreviewLightDark
@Composable
internal fun PanelSizePickerPreview() = SpeecherTheme {
    Surface { PanelSizePicker(PanelSize.Full) {} }
}
