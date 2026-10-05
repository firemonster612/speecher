package app.speecher.android.ui

import android.content.ClipData
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
import androidx.compose.foundation.layout.heightIn
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
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.ClipEntry
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.ButtonLayout
import app.speecher.android.dictation.CleanupProvider
import app.speecher.android.dictation.DictationState
import app.speecher.android.dictation.FailureReason
import app.speecher.android.dictation.InsertAction
import app.speecher.android.dictation.PanelSize
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SpeechProvider
import app.speecher.android.dictation.label
import app.speecher.android.dictation.signInAccount
import app.speecher.android.dictation.spokenLanguageMismatch
import app.speecher.protocol.ProviderFailureKind
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.sin
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch

private const val BAR_COUNT = 29
private const val SAMPLE_MILLIS = 70L

/** Room for the waveform, two lines of transcript and the buttons, about half the full panel. */
private val COMPACT_HEIGHT = 184.dp
private val BAR_HEIGHT = 56.dp
/** The least a half-display cap leaves: the status, a line of transcript and the buttons. */
private val MIN_CAPPED_HEIGHT = 160.dp

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
    onPause: () -> Unit = {},
    onResume: () -> Unit = {},
    transcriptionPreview: Boolean = true,
    refinementPreview: Boolean = true,
) {
    val shown = state.withPreviews(transcriptionPreview, refinementPreview)
    // Sized from the display, not from incoming constraints: inside the IME those are the IME
    // window's own height, so a fraction of them shrinks the panel below the window it sized,
    // leaving an unpainted band at the bottom edge.
    val display = LocalWindowInfo.current.containerDpSize
    val height = panelHeight(size, display.height)
    val status =
        state.wait
            ?: when {
                state is DictationState.Failed -> state.title
                state is DictationState.Listening && state.paused -> "Paused"
                state is DictationState.Listening && state.reconnecting -> "Reconnecting"
                else -> "Listening"
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
                MinimizedBar(state, shown, layout, onCancel, onInsert, onInsertRefined)
            }
            return@Surface
        }
        val compact = size == PanelSize.Compact
        val padding =
            Modifier.padding(
                start = 16.dp,
                end = 16.dp,
                top = if (compact || display.width > display.height) 8.dp else 20.dp,
                bottom = 12.dp,
            )
        val buttons: @Composable () -> Unit = {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                PanelButtons(state, layout, onCancel, onInsert, onInsertRefined, onRecover)
            }
        }
        if (state is DictationState.Failed) {
            // A failure never collapses, so its recovery stays in view. The message sits right
            // above its buttons, and the panel grows rather than clip it at large font sizes.
            Column(
                Modifier.navigationBarsPadding().heightIn(min = height).then(padding),
                verticalArrangement = Arrangement.spacedBy(12.dp, Alignment.CenterVertically),
            ) {
                FailureMessage(state, Modifier.fillMaxWidth().then(announced))
                if (shown.transcript.isNotBlank()) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Transcript(shown, Modifier.weight(1f).heightIn(max = 96.dp))
                        CopyTranscriptButton(state.transcript)
                    }
                }
                buttons()
            }
            return@Surface
        }
        Column(Modifier.navigationBarsPadding().height(height).then(padding)) {
            Box(
                Modifier.fillMaxWidth().heightIn(min = 56.dp).then(announced),
                contentAlignment = Alignment.Center,
            ) {
                val wait = state.wait
                if (state is DictationState.Listening && wait == null) {
                    // Pause, which resumes while paused, to the left of the waveform; the panel's
                    // Cancel button already throws the dictation away.
                    IconButton(
                        if (state.paused) onResume else onPause,
                        Modifier.align(Alignment.CenterStart),
                    ) {
                        Icon(
                            painterResource(
                                if (state.paused) R.drawable.ic_play else R.drawable.ic_pause
                            ),
                            contentDescription = if (state.paused) "Resume" else "Pause",
                        )
                    }
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        LiveBars(state.level)
                        if (state.paused) {
                            Text(
                                "Paused",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                        if (state.reconnecting) {
                            Text(
                                "Reconnecting…",
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                } else {
                    // The microphone is off: the bars say the panel is busy, and the label what
                    // it is waiting on.
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        RefiningBars()
                        if (wait != null) {
                            Text(
                                wait,
                                style = MaterialTheme.typography.labelSmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                            )
                        }
                    }
                }
                IconButton(onToggleSize, Modifier.align(Alignment.CenterEnd)) {
                    Icon(painterResource(R.drawable.ic_minimize), contentDescription = "Minimize")
                }
            }
            Transcript(
                shown,
                Modifier.weight(1f).fillMaxWidth().padding(vertical = if (compact) 4.dp else 12.dp),
                // Until the first words; with the live text off, no words replace it.
                if (state is DictationState.Listening && !state.stopping && state.text.isEmpty())
                    "Speak now"
                else "",
            )
            buttons()
        }
    }
}

/**
 * The panel's height above the navigation bar at [size], on a display [displayHeight] tall. Never
 * more than half the display, so the field being dictated into stays in view in landscape.
 */
internal fun panelHeight(size: PanelSize, displayHeight: Dp): Dp {
    val cap = (displayHeight * 0.5f).coerceAtLeast(MIN_CAPPED_HEIGHT)
    return when (size) {
        PanelSize.Full -> (displayHeight * 0.38f).coerceIn(240.dp, 360.dp).coerceAtMost(cap)
        PanelSize.Compact -> COMPACT_HEIGHT.coerceAtMost(cap)
        PanelSize.Minimized -> BAR_HEIGHT
    }
}

/**
 * [state] as the panel previews its words: a preview turned off in Settings shows none. The
 * transcription preview covers the words heard, also as the dimmed text a refinement starts from
 * and under a failure, and the refinement preview the cleaned text streaming in. Only the shown
 * words go: a failure's Insert still inserts its transcript.
 */
internal fun DictationState.withPreviews(
    transcription: Boolean,
    refinement: Boolean,
): DictationState =
    when (this) {
        is DictationState.Listening ->
            if (transcription) this else copy(committed = "", interim = "")
        is DictationState.Refining ->
            copy(
                transcript = if (transcription) transcript else "",
                refined = if (refinement) refined else "",
            )
        is DictationState.Failed -> if (transcription) this else copy(transcript = "")
    }

/**
 * The collapsed panel: Cancel, a recording dot and a small waveform, the newest words of [shown] on
 * one line (cut at the start so the latest stay visible), and the primary Insert.
 */
@Composable
private fun RowScope.MinimizedBar(
    state: DictationState,
    shown: DictationState,
    layout: ButtonLayout,
    onCancel: () -> Unit,
    onInsert: () -> Unit,
    onInsertRefined: () -> Unit,
) {
    val colors = MaterialTheme.colorScheme
    IconButton(onCancel) {
        Icon(painterResource(R.drawable.ic_close), contentDescription = "Cancel dictation")
    }
    val wait = state.wait
    if (state is DictationState.Listening && wait == null) {
        Canvas(Modifier.size(8.dp)) { drawCircle(colors.error) }
        LiveBars(state.level, SMALL_BAR_WIDTH, SMALL_BARS_HEIGHT)
    } else {
        RefiningBars(SMALL_BAR_WIDTH, SMALL_BARS_HEIGHT)
    }
    val words =
        if (shown is DictationState.Refining && shown.refined.isNotEmpty()) shown.refined
        else shown.transcript
    Text(
        words.ifEmpty { wait ?: if (state is DictationState.Listening) "Listening" else "" },
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
private fun Transcript(state: DictationState, modifier: Modifier, placeholder: String = "") {
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
    val button = Modifier.weight(1f).heightIn(min = 52.dp)
    TextButton(onCancel, Modifier.heightIn(min = 52.dp)) { Text("Cancel") }
    if (state is DictationState.Failed) {
        // On a commit failure the recovery button already re-commits the same text, so a second
        // Insert would duplicate it; show only the recovery action there.
        if (state.reason != FailureReason.Commit && state.transcript.isNotBlank()) {
            FilledTonalButton(onInsert, button) { Text("Insert") }
        }
        Button(onRecover, button) { Text(state.reason.recovery) }
        return
    }
    // While the cleanup runs, Insert stops it and inserts the words as heard, whatever the layout.
    val cleaningUp = state is DictationState.Refining && !state.transcribingAgain
    val secondary = if (cleaningUp) listOf(InsertAction.Insert) else layout.actions.dropLast(1)
    secondary.forEach { action ->
        FilledTonalButton(
            action.pick(onInsert, onInsertRefined),
            Modifier.heightIn(min = 52.dp),
            enabled = cleaningUp || state.canInsert,
        ) {
            Text(action.label, maxLines = 1)
        }
    }
    PrimaryInsertButton(state, layout, onInsert, onInsertRefined, button)
}

private fun InsertAction.pick(onInsert: () -> Unit, onInsertRefined: () -> Unit) =
    if (this == InsertAction.Insert) onInsert else onInsertRefined

private val DictationState.canInsert: Boolean
    get() = this is DictationState.Listening && !stopping && text.isNotBlank()

/**
 * What the panel waits on once Insert is tapped, in the desktop's words, or null while it is not
 * waiting: the last words, ChatGPT's second transcription pass, then the cleanup.
 */
private val DictationState.wait: String?
    get() =
        when {
            this is DictationState.Listening && stopping -> "Transcribing…"
            this is DictationState.Refining && transcribingAgain ->
                "Transcribing again for accuracy…"
            this is DictationState.Refining -> "Refining…"
            else -> null
        }

/**
 * The layout's filled button. Both buttons wait the same way (the transcription pass runs on each),
 * so this one carries the progress whichever was tapped.
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
        val wait = state.wait
        if (wait != null) {
            CircularProgressIndicator(
                Modifier.size(18.dp).semantics { contentDescription = wait },
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

/** Which part failed: the speech service, the cleanup after it, or the phone itself. */
private val DictationState.Failed.title: String
    get() =
        when (reason) {
            FailureReason.Commit -> "Couldn't insert the text"
            FailureReason.MicrophoneDenied -> "Speecher can't use the microphone"
            FailureReason.MicrophoneUnavailable -> "Microphone unavailable"
            FailureReason.SpokenLanguage -> "Spoken language not available"
            is FailureReason.Speech -> "Transcription failed"
            is FailureReason.Cleanup -> "Cleanup failed"
        }

/**
 * What went wrong and what to do next, in fixed words; a provider's in the desktop's. The raw
 * detail is diagnostic and never shown, except a spoken language mismatch's, which is already those
 * words.
 */
private val DictationState.Failed.advice: String
    get() =
        when (val reason = reason) {
            FailureReason.Commit -> "Tap Retry to insert it again."
            FailureReason.MicrophoneDenied -> "Open Speecher and allow the microphone."
            FailureReason.MicrophoneUnavailable ->
                "Another app may be using it. Try again when it's free."
            FailureReason.SpokenLanguage -> detail
            is FailureReason.ProviderFailed -> providerAdvice(reason.account.label, reason.kind)
        }

private fun providerAdvice(name: String, kind: ProviderFailureKind): String =
    when (kind) {
        ProviderFailureKind.Authentication ->
            "Your $name sign-in has expired. Sign in again to keep dictating."
        ProviderFailureKind.Unavailable ->
            "You're not signed in to $name. Sign in to keep dictating."
        ProviderFailureKind.Network ->
            "$name couldn't be reached. Check your connection and try again."
        ProviderFailureKind.Timeout -> "$name didn't answer. Try again."
        ProviderFailureKind.Server -> "$name had a server error. Try again."
        ProviderFailureKind.RateLimited -> "$name hit a usage limit. Try again later."
        ProviderFailureKind.InvalidResult -> "$name's answer was empty or cut short. Try again."
        ProviderFailureKind.Other,
        ProviderFailureKind.Cancelled -> "$name returned an error. Try again."
    }

private val FailureReason.recovery: String
    get() =
        when {
            signInAccount != null -> "Sign in"
            this == FailureReason.MicrophoneDenied || this == FailureReason.SpokenLanguage ->
                "Open Speecher"
            else -> "Retry"
        }

@Composable
private fun FailureMessage(state: DictationState.Failed, modifier: Modifier) {
    val colors = MaterialTheme.colorScheme
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Row(
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(
                painterResource(R.drawable.ic_error),
                contentDescription = null,
                tint = colors.error,
            )
            Text(state.title, style = MaterialTheme.typography.titleMedium, color = colors.error)
        }
        Text(
            state.advice,
            style = MaterialTheme.typography.bodyMedium,
            color = colors.onSurfaceVariant,
            textAlign = TextAlign.Center,
        )
    }
}

/** How long Copy shows its check, the desktop's. */
private const val COPIED_FEEDBACK_MILLIS = 1_500L

/** Puts [text] on the clipboard when tapped, then shows a check for a moment, as on the desktop. */
@Composable
internal fun CopyTranscriptButton(text: String) {
    val clipboard = LocalClipboard.current
    val scope = rememberCoroutineScope()
    var copied by remember { mutableStateOf(false) }
    LaunchedEffect(copied) {
        if (!copied) return@LaunchedEffect
        delay(COPIED_FEEDBACK_MILLIS)
        copied = false
    }
    IconButton({
        scope.launch {
            clipboard.setClipEntry(ClipEntry(ClipData.newPlainText("Transcript", text)))
        }
        copied = true
    }) {
        Icon(
            painterResource(if (copied) R.drawable.ic_check else R.drawable.ic_copy),
            contentDescription = if (copied) "Copied" else "Copy transcript",
        )
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
    transcriptionPreview: Boolean = true,
    refinementPreview: Boolean = true,
) {
    SpeecherTheme {
        DictationPanel(
            state,
            layout,
            size,
            {},
            {},
            {},
            {},
            {},
            transcriptionPreview = transcriptionPreview,
            refinementPreview = refinementPreview,
        )
    }
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
internal fun PanelLiveTextOffPreview() =
    PanelPreview(DictationState.Listening(SAMPLE_TEXT, "", 0.7f), transcriptionPreview = false)

@PreviewLightDark
@Composable
internal fun PanelMinimizedLiveTextOffPreview() =
    PanelPreview(
        DictationState.Listening(SAMPLE_TEXT, "", 0.7f),
        size = PanelSize.Minimized,
        transcriptionPreview = false,
    )

@PreviewLightDark
@Composable
internal fun PanelReconnectingPreview() =
    PanelPreview(DictationState.Listening(SAMPLE_TEXT, "", 0.5f, reconnecting = true))

@PreviewLightDark
@Composable
internal fun PanelStoppingPreview() =
    PanelPreview(DictationState.Listening(SAMPLE_TEXT, "", 0f, stopping = true))

@PreviewLightDark
@Composable
internal fun PanelTranscribingAgainPreview() =
    PanelPreview(DictationState.Refining(SAMPLE_TEXT, transcribingAgain = true))

@PreviewLightDark
@Composable
internal fun PanelRefiningPreview() = PanelPreview(DictationState.Refining(SAMPLE_TEXT))

@PreviewLightDark
@Composable
internal fun PanelRefiningRefinedOnlyPreview() =
    PanelPreview(DictationState.Refining(SAMPLE_TEXT), ButtonLayout.RefinedOnly)

@PreviewLightDark
@Composable
internal fun PanelRefiningStreamPreview() =
    PanelPreview(
        DictationState.Refining(SAMPLE_TEXT, "Can we move the design review to Thursday afternoon?")
    )

/** A failure for [reason], with some words heard before it when [heard]. */
@Composable
internal fun PanelFailedPreview(reason: FailureReason, heard: Boolean = false) =
    PanelPreview(
        DictationState.Failed(
            reason,
            if (reason == FailureReason.SpokenLanguage)
                spokenLanguageMismatch(Provider.Claude, "cy").orEmpty()
            else "",
            if (heard) SAMPLE_TEXT else "",
        )
    )

@PreviewLightDark
@Composable
internal fun PanelRefiningLiveTextOffPreview() =
    PanelPreview(
        DictationState.Refining(
            SAMPLE_TEXT,
            "Can we move the design review to Thursday afternoon?",
        ),
        refinementPreview = false,
    )

@PreviewLightDark
@Composable
internal fun PanelFailedMicrophonePreview() = PanelFailedPreview(FailureReason.MicrophoneDenied)

@PreviewLightDark
@Composable
internal fun PanelFailedSignedOutPreview() =
    PanelFailedPreview(FailureReason.Speech(SpeechProvider.Claude, ProviderFailureKind.Unavailable))

@PreviewLightDark
@Composable
internal fun PanelFailedNetworkPreview() =
    PanelFailedPreview(
        FailureReason.Speech(SpeechProvider.Claude, ProviderFailureKind.Network),
        heard = true,
    )

@PreviewLightDark
@Composable
internal fun PanelFailedCleanupPreview() =
    PanelFailedPreview(
        FailureReason.Cleanup(CleanupProvider.Claude, ProviderFailureKind.RateLimited),
        heard = true,
    )

@PreviewLightDark
@Composable
internal fun PanelFailedNetworkLiveTextOffPreview() =
    PanelPreview(
        DictationState.Failed(
            FailureReason.Speech(SpeechProvider.Claude, ProviderFailureKind.Network),
            "",
            SAMPLE_TEXT,
        ),
        transcriptionPreview = false,
    )

@PreviewLightDark
@Composable
internal fun PanelFailedSpokenLanguagePreview() = PanelFailedPreview(FailureReason.SpokenLanguage)
