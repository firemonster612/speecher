package app.speecher.android.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.dictation.ButtonLayout
import app.speecher.android.dictation.DictationState
import app.speecher.android.dictation.FailureReason
import app.speecher.android.dictation.InsertAction
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin
import kotlinx.coroutines.delay

private const val BAR_COUNT = 29
private const val SAMPLE_MILLIS = 70L

/**
 * A bell from 0 at the edges to 1 in the middle, with a little irregularity so it reads as sound.
 */
private val Envelope =
    List(BAR_COUNT) { i ->
        val bell = sin(PI.toFloat() * (i + 0.5f) / BAR_COUNT)
        bell * bell * (0.8f + 0.2f * abs(sin(i * 2.1f)))
    }

/**
 * The IME's input view. It draws its first frame complete, with no enter animation, because the
 * swap already leaves a blank gap before it appears (m2-spike-findings, "Frame-level flicker").
 */
@Composable
fun DictationPanel(
    state: DictationState,
    layout: ButtonLayout,
    onCancel: () -> Unit,
    onInsert: () -> Unit,
    onInsertRefined: () -> Unit,
    onRecover: () -> Unit,
    modifier: Modifier = Modifier,
) {
    // Sized from the display, not from incoming constraints: inside the IME those are the IME
    // window's own height, so a fraction of them shrinks the panel below the window it sized,
    // leaving an unpainted band at the bottom edge.
    val panelHeight =
        (LocalWindowInfo.current.containerDpSize.height * 0.38f).coerceIn(240.dp, 360.dp)
    Surface(modifier.fillMaxWidth(), color = MaterialTheme.colorScheme.surfaceContainer) {
        Column(
            Modifier.navigationBarsPadding()
                .height(panelHeight)
                .padding(start = 16.dp, end = 16.dp, top = 20.dp, bottom = 12.dp)
        ) {
            val status =
                when (state) {
                    is DictationState.Listening ->
                        if (state.reconnecting) "Reconnecting" else "Listening"
                    is DictationState.Refining -> "Refining transcript"
                    is DictationState.Failed -> state.reason.title
                }
            Box(
                Modifier.fillMaxWidth().height(56.dp).semantics {
                    liveRegion = LiveRegionMode.Polite
                    stateDescription = status
                },
                contentAlignment = Alignment.Center,
            ) {
                when (state) {
                    is DictationState.Listening ->
                        Column(horizontalAlignment = Alignment.CenterHorizontally) {
                            LiveBars(state.level)
                            if (state.reconnecting) {
                                Text(
                                    "Reconnecting…",
                                    style = MaterialTheme.typography.labelSmall,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                                )
                            }
                        }
                    is DictationState.Refining -> RefiningBars()
                    is DictationState.Failed -> FailureMessage(state)
                }
            }
            Transcript(state, Modifier.weight(1f).fillMaxWidth().padding(vertical = 12.dp))
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                PanelButtons(
                    state,
                    layout,
                    onCancel,
                    onInsert,
                    onInsertRefined,
                    onRecover,
                )
            }
        }
    }
}

private val DictationState.transcript: String
    get() =
        when (this) {
            is DictationState.Listening -> text
            is DictationState.Refining -> transcript
            is DictationState.Failed -> transcript
        }

@Composable
private fun Transcript(state: DictationState, modifier: Modifier) {
    // Refining shows the raw transcript dimmed until the cleanup's first token, then the cleaned
    // text as it streams in, through the same append-only preview as live dictation.
    val refined = (state as? DictationState.Refining)?.refined.orEmpty()
    val committed =
        (state as? DictationState.Listening)?.committed ?: refined.ifEmpty { state.transcript }
    val interim = (state as? DictationState.Listening)?.interim.orEmpty()
    val scroll = rememberScrollState()
    // Follow the bottom off layout, not the text: jumping to maxValue after each relayout keeps the
    // newest words in view without an animation chasing a one-frame-stale target, and shrinking
    // interim text no longer lurches the preview up then back down.
    LaunchedEffect(scroll) { snapshotFlow { scroll.maxValue }.collect { scroll.scrollTo(it) } }
    val placeholder = if (state is DictationState.Listening) "Speak now" else ""
    val colors = MaterialTheme.colorScheme
    Box(modifier.verticalScroll(scroll)) {
        if (committed.isEmpty() && interim.isEmpty()) {
            Text(
                placeholder,
                style = MaterialTheme.typography.bodyLarge,
                color = colors.onSurfaceVariant,
            )
        } else {
            Text(
                buildAnnotatedString {
                    append(committed)
                    if (committed.isNotEmpty() && interim.isNotEmpty()) append(' ')
                    if (interim.isNotEmpty()) {
                        withStyle(SpanStyle(color = colors.onSurfaceVariant)) { append(interim) }
                    }
                },
                style = MaterialTheme.typography.bodyLarge,
                color =
                    if (state is DictationState.Refining && refined.isEmpty())
                        colors.onSurface.copy(alpha = 0.6f)
                    else colors.onSurface,
            )
        }
    }
}

@Composable
private fun RowScope.PanelButtons(
    state: DictationState,
    layout: ButtonLayout,
    onCancel: () -> Unit,
    onInsert: () -> Unit,
    onInsertRefined: () -> Unit,
    onRecover: () -> Unit,
) {
    val button = Modifier.weight(1f).height(52.dp)
    TextButton(onCancel, Modifier.height(52.dp)) { Text("Cancel") }
    if (state is DictationState.Failed) {
        // On a commit failure the recovery button already re-commits the same text, so a second
        // Insert would duplicate it; show only the recovery action there.
        if (!state.commitFailed && state.transcript.isNotBlank()) {
            FilledTonalButton(onInsert, button) { Text("Insert") }
        }
        Button(onRecover, button) { Text(state.reason.recovery) }
        return
    }
    val canInsert = state is DictationState.Listening && state.text.isNotBlank()
    fun InsertAction.onClick() = if (this == InsertAction.Insert) onInsert else onInsertRefined
    layout.actions.dropLast(1).forEach { action ->
        FilledTonalButton(action.onClick(), Modifier.height(52.dp), enabled = canInsert) {
            Text(action.label, maxLines = 1)
        }
    }
    // Both buttons pass through Refining (the transcription pass runs on each), so the filled one
    // carries the progress whichever was tapped.
    val primary = layout.actions.last()
    Button(primary.onClick(), button, enabled = canInsert) {
        if (state is DictationState.Refining) {
            CircularProgressIndicator(
                Modifier.size(18.dp).semantics { contentDescription = "Refining transcript" },
                strokeWidth = 2.dp,
            )
        } else {
            Text(primary.label, maxLines = 1)
        }
    }
}

private val InsertAction.label: String
    get() =
        when (this) {
            InsertAction.Insert -> "Insert"
            InsertAction.InsertRefined -> "Insert refined"
        }

private val FailureReason.title: String
    get() =
        when (this) {
            FailureReason.MicrophoneDenied -> "Speecher can't use the microphone"
            FailureReason.SignedOut -> "You're signed out"
            FailureReason.Network -> "No connection"
            FailureReason.Provider -> "Transcription failed"
        }

private val FailureReason.recovery: String
    get() =
        when (this) {
            FailureReason.MicrophoneDenied -> "Open Speecher"
            FailureReason.SignedOut -> "Sign in"
            FailureReason.Network,
            FailureReason.Provider -> "Retry"
        }

@Composable
private fun FailureMessage(state: DictationState.Failed) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(
            state.reason.title,
            style = MaterialTheme.typography.titleMedium,
            color = MaterialTheme.colorScheme.error,
        )
        if (state.detail.isNotBlank()) {
            Text(
                state.detail,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 1,
            )
        }
    }
}

/** Scrolls the input level through the bars, newest on the right. */
@Composable
private fun LiveBars(level: Float) {
    val latest by rememberUpdatedState(level)
    // Starts full of the current level so the first frame is already a waveform, not a blank.
    var history by remember { mutableStateOf(List(BAR_COUNT) { level }) }
    LaunchedEffect(Unit) {
        while (true) {
            delay(SAMPLE_MILLIS)
            history = history.drop(1) + latest
        }
    }
    // The envelope keeps the mark's silhouette, tall in the middle, but only softens the edges now
    // that the level spans the range, so speech visibly moves every bar instead of a faint few.
    Bars(
        history.mapIndexed { i, sample -> sample * (0.5f + 0.5f * Envelope[i]) },
        MaterialTheme.colorScheme.onSurface,
    )
}

@Composable
private fun RefiningBars() {
    val phase by
        rememberInfiniteTransition()
            .animateFloat(
                0f,
                2 * PI.toFloat(),
                infiniteRepeatable(tween(1200, easing = LinearEasing)),
            )
    Bars(
        List(BAR_COUNT) { 0.2f + 0.2f * (1 + sin(phase - it * 0.45f)) / 2 },
        MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/** Rounded bars in the mark's proportions: 7 wide on a 13 step. [levels] run from 0 to 1. */
@Composable
private fun Bars(levels: List<Float>, color: Color) {
    val barWidth = 4.dp
    val step = barWidth * 13 / 7
    Canvas(Modifier.width(step * (levels.size - 1) + barWidth).height(40.dp)) {
        val width = barWidth.toPx()
        levels.forEachIndexed { index, level ->
            val height = width + (size.height - width) * level.coerceIn(0f, 1f)
            drawRoundRect(
                color,
                Offset(step.toPx() * index, (size.height - height) / 2),
                Size(width, height),
                CornerRadius(width / 2),
            )
        }
    }
}

@Composable
private fun PanelPreview(
    state: DictationState,
    layout: ButtonLayout = ButtonLayout.RefinedPrimary,
) {
    SpeecherTheme { DictationPanel(state, layout, {}, {}, {}, {}) }
}

private const val SAMPLE_TEXT =
    "Can we move the design review to Thursday afternoon? I'd like Priya to walk us through the " +
        "new onboarding flow before we lock it"

@PreviewLightDark
@Composable
internal fun PanelListeningEmptyPreview() = PanelPreview(DictationState.Listening(level = 0.1f))

@PreviewLightDark
@Composable
internal fun PanelListeningPreview() = PanelPreview(DictationState.Listening(SAMPLE_TEXT, "", 0.7f))

@PreviewLightDark
@Composable
internal fun PanelListeningNoRefinePreview() =
    PanelPreview(DictationState.Listening(SAMPLE_TEXT, "", 0.7f), ButtonLayout.InsertOnly)

@PreviewLightDark
@Composable
internal fun PanelListeningRefinedOnlyPreview() =
    PanelPreview(DictationState.Listening(SAMPLE_TEXT, "", 0.7f), ButtonLayout.RefinedOnly)

@PreviewLightDark
@Composable
internal fun PanelReconnectingPreview() =
    PanelPreview(DictationState.Listening(SAMPLE_TEXT, "", 0.5f, reconnecting = true))

@PreviewLightDark
@Composable
internal fun PanelRefiningPreview() = PanelPreview(DictationState.Refining(SAMPLE_TEXT))

@PreviewLightDark
@Composable
internal fun PanelRefiningStreamPreview() =
    PanelPreview(
        DictationState.Refining(SAMPLE_TEXT, "Can we move the design review to Thursday afternoon?")
    )

@PreviewLightDark
@Composable
internal fun PanelFailedMicrophonePreview() =
    PanelPreview(DictationState.Failed(FailureReason.MicrophoneDenied, "", ""))

@PreviewLightDark
@Composable
internal fun PanelFailedSignedOutPreview() =
    PanelPreview(DictationState.Failed(FailureReason.SignedOut, "Claude session expired", ""))

@PreviewLightDark
@Composable
internal fun PanelFailedNetworkPreview() =
    PanelPreview(
        DictationState.Failed(FailureReason.Network, "Lost the connection", "Can we move the")
    )

@PreviewLightDark
@Composable
internal fun PanelFailedProviderPreview() =
    PanelPreview(DictationState.Failed(FailureReason.Provider, "HTTP 503 from Claude", ""))
