package app.speecher.android.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.IntOffset
import androidx.compose.ui.unit.dp
import kotlin.math.roundToInt

/**
 * Places the dictation button for when it isn't docked on the mic key. The block at the bottom
 * stands in for the keyboard. The saved position is the chip's top-left as a pixel offset from the
 * block's bottom-right corner, the same anchor the overlay uses on the real keyboard.
 */
@Composable
fun ChipPosition(offsetX: Int?, offsetY: Int?, onSave: (Int, Int) -> Unit) {
    val density = LocalDensity.current
    val chipWidth = with(density) { ChipSize.width.roundToPx() }
    val chipHeight = with(density) { ChipSize.height.roundToPx() }
    val margin = with(density) { ChipMargin.roundToPx() }
    // With nothing saved, start where the overlay's fallback corner puts it.
    var x by rememberSaveable { mutableIntStateOf(offsetX ?: -(chipWidth + margin)) }
    var y by rememberSaveable { mutableIntStateOf(offsetY ?: -(chipHeight + margin)) }
    var startX by remember { mutableIntStateOf(0) }
    var startY by remember { mutableIntStateOf(0) }
    Column {
        Text(
            "Drag the button to where it should sit over your keyboard.",
            Modifier.padding(16.dp),
            style = MaterialTheme.typography.bodyLarge,
        )
        BoxWithConstraints(Modifier.fillMaxWidth().height(440.dp)) {
            val width = constraints.maxWidth
            val height = constraints.maxHeight
            Surface(
                Modifier.align(Alignment.BottomCenter).fillMaxWidth().height(300.dp),
                color = MaterialTheme.colorScheme.surfaceContainerHigh,
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Text("Keyboard", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            }
            DictationChip(
                onTap = {},
                onDragStart = {
                    startX = x
                    startY = y
                },
                onDrag = { dx, dy ->
                    x = (startX + dx.roundToInt()).coerceIn(-width, -chipWidth)
                    y = (startY + dy.roundToInt()).coerceIn(-height, -chipHeight)
                },
                Modifier.offset { IntOffset(width + x, height + y) },
            )
        }
        Button({ onSave(x, y) }, Modifier.padding(16.dp).fillMaxWidth()) { Text("Save position") }
    }
}

@PreviewLightDark
@Composable
internal fun ChipPositionPreview() = SpeecherTheme {
    Surface { ChipPosition(null, null) { _, _ -> } }
}
