package app.speecher.android.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LifecycleEventEffect
import app.speecher.android.dictation.Microphone

/** A microphone test that stops when its screen goes away or the app leaves the foreground. */
@Composable
private fun rememberMicrophoneTest(
    capture: (() -> Boolean, (ByteArray, Float) -> Unit) -> Unit
): MicrophoneTest {
    val test = remember { MicrophoneTest(capture) }
    DisposableEffect(test) { onDispose(test::stop) }
    LifecycleEventEffect(Lifecycle.Event.ON_STOP) { test.stop() }
    return test
}

@Composable
private fun rememberMicrophoneTest(): MicrophoneTest {
    val context = LocalContext.current.applicationContext
    // A microphone per run: a stopped run's capture may still be closing its recorder.
    return rememberMicrophoneTest { shouldContinue, onAudio ->
        Microphone(context).capture(shouldContinue, onAudio)
    }
}

/** The Test microphone row of Settings. */
@Composable
internal fun MicrophoneTestRow(test: MicrophoneTest = rememberMicrophoneTest()) {
    ListItem(
        headlineContent = { Text("Test microphone") },
        supportingContent = { MicrophoneTestControls(test) },
        colors = rowColors(),
    )
}

/** What the test is for, a level meter beside its button, and what it has found. */
@Composable
internal fun MicrophoneTestControls(test: MicrophoneTest = rememberMicrophoneTest()) {
    Column {
        Text(
            "Speak and watch the level to check that the microphone hears you.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Row(
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            LinearProgressIndicator(
                { test.level },
                Modifier.weight(1f).semantics { contentDescription = "Input level" },
            )
            FilledTonalButton(test::toggle) {
                Text(if (test.running) "Stop test" else "Start test")
            }
        }
        test.status?.let {
            Text(
                it,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

/**
 * The Transcription page's row with a test started on [capture], standing in for the microphone.
 */
@Composable
private fun MicrophoneTestPreview(capture: (() -> Boolean, (ByteArray, Float) -> Unit) -> Unit) =
    SpeecherTheme {
        val test = rememberMicrophoneTest(capture)
        LaunchedEffect(test) { test.start() }
        SpeecherScreen(SettingsPage.Transcription.title, onBack = {}) {
            Section("Microphone")
            MicrophoneTestRow(test)
        }
    }

/** Hears [audio] at [level], then keeps the test running until it stops. */
private fun heard(
    audio: ByteArray,
    level: Float,
): (() -> Boolean, (ByteArray, Float) -> Unit) -> Unit = { shouldContinue, onAudio ->
    onAudio(audio, level)
    while (shouldContinue()) Thread.sleep(100)
}

@PreviewLightDark
@Composable
internal fun MicrophoneTestDetectedPreview() =
    MicrophoneTestPreview(heard(ByteArray(3200) { 0x10 }, 0.6f))

@PreviewLightDark
@Composable
internal fun MicrophoneTestSilentPreview() = MicrophoneTestPreview(heard(ByteArray(160_000), 0f))

@PreviewLightDark
@Composable
internal fun MicrophoneTestNoAccessPreview() = MicrophoneTestPreview { _, _ ->
    throw SecurityException()
}
