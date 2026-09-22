package app.speecher.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import app.speecher.android.dictation.DictationState

/** Temporary surface for the IME and chip until the UI track supplies its design. */
@Composable
fun EngineSurface(
    state: DictationState?,
    refinementEnabled: Boolean,
    onTap: () -> Unit,
    onCancel: () -> Unit,
    onInsert: () -> Unit,
    onInsertRefined: () -> Unit,
) {
    if (state == null) {
        Button(onClick = onTap) { Text("●") }
        return
    }
    Column {
        Text(state.toString())
        Button(onClick = onCancel) { Text("Cancel") }
        Button(onClick = onInsert) { Text("Insert") }
        if (refinementEnabled) Button(onClick = onInsertRefined) { Text("Insert refined") }
    }
}
