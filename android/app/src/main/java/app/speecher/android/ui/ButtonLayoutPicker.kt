package app.speecher.android.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.RadioButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.dictation.ButtonLayout
import app.speecher.android.dictation.InsertAction

private val ButtonLayout.label: String
    get() =
        when (this) {
            ButtonLayout.RefinedPrimary -> "Refined + Insert"
            ButtonLayout.InsertOnly -> "Insert only"
            ButtonLayout.RefinedOnly -> "Refined only"
        }

/** One radio row per [ButtonLayout], each with a sketch of the panel's buttons. */
@Composable
fun ButtonLayoutPicker(selected: ButtonLayout, onSelect: (ButtonLayout) -> Unit) {
    Column(Modifier.selectableGroup()) {
        ButtonLayout.entries.forEach { layout ->
            Row(
                Modifier.fillMaxWidth()
                    .selectable(layout == selected, role = Role.RadioButton) { onSelect(layout) }
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                RadioButton(layout == selected, onClick = null)
                ButtonLayoutIllustration(layout, Modifier.padding(start = 16.dp))
                Text(
                    layout.label,
                    Modifier.padding(start = 16.dp),
                    style = MaterialTheme.typography.bodyLarge,
                )
            }
        }
    }
}

/**
 * A flat sketch of the dictation panel: two lines of transcript above a Cancel pill and the
 * layout's buttons, in the same sizes and colours the panel gives them. Refined buttons carry a
 * sparkle.
 */
@Composable
private fun ButtonLayoutIllustration(layout: ButtonLayout, modifier: Modifier = Modifier) {
    val colors = MaterialTheme.colorScheme
    Canvas(modifier.size(112.dp, 64.dp)) {
        val unit = size.height / 16
        drawRoundRect(colors.surfaceContainerHighest, cornerRadius = CornerRadius(unit * 2))
        val line = colors.onSurfaceVariant.copy(alpha = 0.4f)
        listOf(0.8f, 0.55f).forEachIndexed { index, fraction ->
            drawRoundRect(
                line,
                Offset(unit * 2, unit * (2.5f + index * 2.5f)),
                Size((size.width - unit * 4) * fraction, unit * 1.2f),
                CornerRadius(unit * 0.6f),
            )
        }
        val top = size.height - unit * 6.5f
        val buttonHeight = unit * 4.5f
        var left = unit * 2
        // Cancel is a text button in the panel, so only a faint label.
        drawRoundRect(
            colors.onSurfaceVariant.copy(alpha = 0.5f),
            Offset(left, top + buttonHeight / 2 - unit * 0.6f),
            Size(unit * 4, unit * 1.2f),
            CornerRadius(unit * 0.6f),
        )
        left += unit * 5.5f
        val right = size.width - unit * 2
        val secondaryWidth = unit * 7
        layout.actions.forEach { action ->
            val primary = action == layout.actions.last()
            val width = if (primary) right - left else secondaryWidth
            drawButton(
                Offset(left, top),
                Size(width, buttonHeight),
                if (primary) colors.primary else colors.secondaryContainer,
                if (primary) colors.onPrimary else colors.onSecondaryContainer,
                action == InsertAction.InsertRefined,
            )
            left += width + unit
        }
    }
}

private fun DrawScope.drawButton(
    topLeft: Offset,
    size: Size,
    container: Color,
    content: Color,
    refined: Boolean,
) {
    drawRoundRect(container, topLeft, size, CornerRadius(size.height / 2))
    val center = topLeft + Offset(size.width / 2, size.height / 2)
    val label = size.height * 0.25f
    if (!refined) {
        drawRoundRect(
            content,
            center - Offset(label * 1.5f, label / 2),
            Size(label * 3, label),
            CornerRadius(label / 2),
        )
        return
    }
    // A four-point sparkle, the usual mark for AI-polished text.
    val reach = size.height * 0.28f
    val waist = reach * 0.3f
    val sparkle =
        Path().apply {
            moveTo(center.x, center.y - reach)
            lineTo(center.x + waist, center.y - waist)
            lineTo(center.x + reach, center.y)
            lineTo(center.x + waist, center.y + waist)
            lineTo(center.x, center.y + reach)
            lineTo(center.x - waist, center.y + waist)
            lineTo(center.x - reach, center.y)
            lineTo(center.x - waist, center.y - waist)
            close()
        }
    drawPath(sparkle, content)
}

@PreviewLightDark
@Composable
internal fun ButtonLayoutPickerPreview() = SpeecherTheme {
    Surface { ButtonLayoutPicker(ButtonLayout.RefinedPrimary) {} }
}
