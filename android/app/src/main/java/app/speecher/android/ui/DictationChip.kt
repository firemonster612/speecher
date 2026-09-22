package app.speecher.android.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp

/**
 * The overlay chip docked above the keyboard's top-right corner: the brand pill with its bars, in
 * tonal surface colours so it sits beside Gboard's own keys without competing with them.
 */
@Composable
fun DictationChip(onClick: () -> Unit, modifier: Modifier = Modifier) {
    Surface(
        onClick = onClick,
        modifier =
            modifier.size(width = 52.dp, height = 36.dp).semantics {
                contentDescription = "Dictate"
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
        DictationChip({}, Modifier.padding(16.dp))
    }
}
