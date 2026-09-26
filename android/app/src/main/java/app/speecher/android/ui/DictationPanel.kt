package app.speecher.android.ui

import androidx.compose.animation.core.LinearEasing
import androidx.compose.animation.core.animateFloat
import androidx.compose.animation.core.infiniteRepeatable
import androidx.compose.animation.core.rememberInfiniteTransition
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.clickable
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
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
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
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.ButtonLayout
import app.speecher.android.dictation.DictationState
import app.speecher.android.dictation.FailureReason
import app.speecher.android.dictation.InsertAction
import app.speecher.android.dictation.PanelSize
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin
import kotlinx.coroutines.delay

private const val BAR_COUNT = 29
private const val SAMPLE_MILLIS = 70L

/** Room for the waveform, two lines of transcript and the buttons, about half the full panel. */
private val COMPACT_HEIGHT = 184.dp
private val BAR_HEIGHT = 56.dp

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
 * [onToggleSize] is the minimize control: the full and compact panels collapse to a bar, and
 * tapping the bar expands it.
 */
@Composable
fun DictationPanel(
    state: DictationState,
    layout: ButtonLayout,
    size: PanelSize,
    onToggleSize: () -> Unit,
    onCancel: () -> Unit,
    onInsert: () -> Unit,
    onInsertRefined: () -> Unit,
    onRecover: () -> Unit,
    modifier: Modifier = Modifier,
) {
    // Sized from the display, not from incoming constraints: inside the IME those are the IME
    // window's own height, so a fraction of them shrinks the panel below the window it sized,
    // leaving an unpainted band at the bottom edge.
    val height = panelHeight(size, LocalWindowInfo.current.containerDpSize.height)
    val status =
        when (state) {
            is DictationState.Listening -> if (state.reconnecting) "Reconnecting" else "Listening"
            is DictationState.Refining -> "Refining transcript"
            is DictationState.Failed -> state.reason.title
        }
    val announced = Modifier.semantics {
        liveRegion = LiveRegionMode.Polite
        stateDescription = status
    }
    Surface(modifier.fillMaxWidth(), color = MaterialTheme.colorScheme.surfaceContainer) {
        if (size == PanelSize.Minimized) {
            Row(
                Modifier.navigationBarsPadding()
                    .height(height)
                    .fillMaxWidth()
                    .clickable(onClickLabel = "Expand", onClick = onToggleSize)
                    .then(announced)
                    .padding(horizontal = 16.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                MinimizedBar(state, layout, onInsert, onInsertRefined)
            }
            return@Surface
        }
        val compact = size == PanelSize.Compact
        Column(
            Modifier.navigationBarsPadding()
                .height(height)
                .padding(
                    start = 16.dp,
                    end = 16.dp,
                    top = if (compact) 8.dp else 20.dp,
                    bottom = 12.dp,
                )
        ) {
            Box(
                Modifier.fillMaxWidth().height(56.dp).then(announced),
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
                // A failure never collapses, so its recovery stays in view.
                if (state !is DictationState.Failed) {
                    IconButton(onToggleSize, Modifier.align(Alignment.CenterEnd)) {
                        Icon(
                            painterResource(R.drawable.ic_minimize),
                            contentDescription = "Minimize",
                        )
                    }
                }
            }
            Transcript(
                state,
                Modifier.weight(1f).fillMaxWidth().padding(vertical = if (compact) 4.dp else 12.dp),
            )
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

/** The panel's height above the navigation bar at [size], on a display [displayHeight] tall. */
internal fun panelHeight(size: PanelSize, displayHeight: Dp): Dp =
    when (size) {
        PanelSize.Full -> (displayHeight * 0.38f).coerceIn(240.dp, 360.dp)
        PanelSize.Compact -> COMPACT_HEIGHT
        PanelSize.Minimized -> BAR_HEIGHT
    }

/**
 * The collapsed panel: a recording dot and a small waveform, the newest words on one line (cut at
 * the start so the latest stay visible), and the primary Insert.
 */
@Composable
private fun RowScope.MinimizedBar(
    state: DictationState,
    layout: ButtonLayout,
    onInsert: () -> Unit,
    onInsertRefined: () -> Unit,
) {
    val colors = MaterialTheme.colorScheme
    if (state is DictationState.Listening) {
        Canvas(Modifier.size(8.dp)) { drawCircle(colors.error) }
        LiveBars(state.level, SMALL_BAR_WIDTH, SMALL_BARS_HEIGHT)
    } else {
        RefiningBars(SMALL_BAR_WIDTH, SMALL_BARS_HEIGHT)
    }
    val words =
        if (state is DictationState.Refining && state.refined.isNotEmpty()) state.refined
        else state.transcript
    Text(
        words.ifEmpty { if (state is DictationState.Listening) "Speak now" else "" },
        Modifier.weight(1f),
        color = if (words.isEmpty()) colors.onSurfaceVariant else colors.onSurface,
        overflow = TextOverflow.StartEllipsis,
        maxLines = 1,
        style = MaterialTheme.typography.bodyMedium,
    )
    PrimaryInsertButton(state, layout, onInsert, onInsertRefined, Modifier.height(40.dp))
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
    layout.actions.dropLast(1).forEach { action ->
        FilledTonalButton(
            action.pick(onInsert, onInsertRefined),
            Modifier.height(52.dp),
            enabled = state.canInsert,
        ) {
            Text(action.label, maxLines = 1)
        }
    }
    PrimaryInsertButton(state, layout, onInsert, onInsertRefined, button)
}

private fun InsertAction.pick(onInsert: () -> Unit, onInsertRefined: () -> Unit) =
    if (this == InsertAction.Insert) onInsert else onInsertRefined

private val DictationState.canInsert: Boolean
    get() = this is DictationState.Listening && text.isNotBlank()

/**
 * The layout's filled button. Both buttons pass through Refining (the transcription pass runs on
 * each), so this one carries the progress whichever was tapped.
 */
@Composable
private fun PrimaryInsertButton(
    state: DictationState,
    layout: ButtonLayout,
    onInsert: () -> Unit,
    onInsertRefined: () -> Unit,
    modifier: Modifier,
) {
    val primary = layout.actions.last()
    Button(primary.pick(onInsert, onInsertRefined), modifier, enabled = state.canInsert) {
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

private val SMALL_BAR_WIDTH = 1.5.dp
private val SMALL_BARS_HEIGHT = 24.dp

/** Scrolls the input level through the bars, newest on the right. */
@Composable
private fun LiveBars(level: Float, barWidth: Dp = 4.dp, height: Dp = 40.dp) {
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
        barWidth,
        height,
    )
}

@Composable
private fun RefiningBars(barWidth: Dp = 4.dp, height: Dp = 40.dp) {
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
        barWidth,
        height,
    )
}

/** Rounded bars in the mark's proportions: 7 wide on a 13 step. [levels] run from 0 to 1. */
@Composable
private fun Bars(levels: List<Float>, color: Color, barWidth: Dp, height: Dp) {
    val step = barWidth * 13 / 7
    Canvas(Modifier.width(step * (levels.size - 1) + barWidth).height(height)) {
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
    size: PanelSize = PanelSize.Full,
) {
    SpeecherTheme { DictationPanel(state, layout, size, {}, {}, {}, {}, {}) }
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
internal fun PanelCompactPreview() =
    PanelPreview(DictationState.Listening(SAMPLE_TEXT, "", 0.7f), size = PanelSize.Compact)

@PreviewLightDark
@Composable
internal fun PanelMinimizedPreview() =
    PanelPreview(DictationState.Listening(SAMPLE_TEXT, "", 0.7f), size = PanelSize.Minimized)

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
