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
import androidx.compose.ui.unit.dp

/**
 * The overlay chip docked beside the keyboard: the brand pill with its bars, in tonal surface
 * colours so it sits next to the keyboard's own keys without competing with them. A short tap
 * starts dictation ([onTap]); dragging past the touch slop moves the window ([onDrag]/[onDragEnd])
 * so the user can place it clear of their keyboard's controls.
 */
@Composable
fun DictationChip(
    onTap: () -> Unit,
    onDrag: (Float, Float) -> Unit,
    onDragEnd: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Surface(
        modifier =
            modifier
                .size(width = 52.dp, height = 36.dp)
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
                        var dragging = false
                        // End a drag however the gesture stops (lifted, cancelled or the chip
                        // removed), or the service would keep ignoring keyboard changes.
                        try {
                            while (true) {
                                val change =
                                    awaitPointerEvent().changes.firstOrNull { it.id == down.id }
                                        ?: break
                                if (change.changedToUp()) {
                                    change.consume()
                                    if (!dragging) onTap()
                                    break
                                }
                                // Positions are local to the overlay window, which moves with the
                                // drag, so the distance from the grab point is exactly how far the
                                // window must move to stay under the finger.
                                val offset = change.position - down.position
                                if (!dragging && offset.getDistance() > slop) dragging = true
                                if (dragging) {
                                    onDrag(offset.x, offset.y)
                                    change.consume()
                                }
                            }
                        } finally {
                            if (dragging) onDragEnd()
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
        DictationChip({}, { _, _ -> }, {}, Modifier.padding(16.dp))
    }
}
