package app.speecher.android.ui

import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.changedToUp
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.DpSize
import androidx.compose.ui.unit.dp
import kotlin.math.hypot

/** The chip's size, shared with the overlay window that holds it. */
val ChipSize = DpSize(52.dp, 36.dp)

/** The chip's inset from the keyboard's bottom-right corner when it has nowhere better to sit. */
val ChipMargin = 6.dp

/**
 * The overlay chip docked beside the keyboard: the brand pill with its bars, in tonal surface
 * colours so it sits next to the keyboard's own keys without competing with them. A short tap
 * starts dictation ([onTap]). Dragging past the touch slop calls [onDragStart], then [onDrag] with
 * the finger's travel from where it went down, so the caller can move the chip by that much.
 */
@Composable
fun DictationChip(
    onTap: () -> Unit,
    onDragStart: () -> Unit,
    onDrag: (Float, Float) -> Unit,
    modifier: Modifier = Modifier,
) {
    Surface(
        modifier =
            modifier
                .size(ChipSize)
                .semantics {
                    contentDescription = "Dictate"
                    role = Role.Button
                    // The gesture handler below replaces Surface's click, so keep TalkBack's
                    // double-tap wired to the same action.
                    onClick {
                        onTap()
                        true
                    }
                }
                .pointerInput(Unit) {
                    val slop = viewConfiguration.touchSlop
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        // Raw screen coordinates, not the change's position: the overlay window
                        // moves with the drag, so window-local positions shift under the finger
                        // and feed each move back into the next one.
                        val start = currentEvent.motionEvent ?: return@awaitEachGesture
                        val startX = start.rawX
                        val startY = start.rawY
                        var dragging = false
                        while (true) {
                            val event = awaitPointerEvent()
                            val change = event.changes.firstOrNull { it.id == down.id } ?: break
                            if (change.changedToUp()) {
                                change.consume()
                                if (!dragging) onTap()
                                break
                            }
                            val motion = event.motionEvent ?: continue
                            val dx = motion.rawX - startX
                            val dy = motion.rawY - startY
                            if (!dragging && hypot(dx, dy) > slop) {
                                dragging = true
                                onDragStart()
                            }
                            if (dragging) {
                                onDrag(dx, dy)
                                change.consume()
                            }
                        }
                    }
                },
        shape = CircleShape,
        color = MaterialTheme.colorScheme.surfaceContainerHighest,
        shadowElevation = 2.dp,
    ) {
        Box(contentAlignment = Alignment.Center) {
            MarkBars(
                MarkBarHeights,
                MaterialTheme.colorScheme.onSurfaceVariant,
                Modifier.padding(horizontal = 6.dp),
            )
        }
    }
}

@PreviewLightDark
@Composable
internal fun ChipPreview() = SpeecherTheme {
    Surface(color = MaterialTheme.colorScheme.background) {
        DictationChip({}, {}, { _, _ -> }, Modifier.padding(16.dp))
    }
}
