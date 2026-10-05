package app.speecher.android.ui

import android.content.pm.PackageInstaller
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.consumeWindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.imePadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.LatestTranscript
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SetupStatus
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.countWords
import app.speecher.android.dictation.label
import app.speecher.android.dictation.providerOrder
import app.speecher.android.dictation.resolveSignedIn
import app.speecher.android.update.UpdateState
import app.speecher.android.update.installFailure
import java.text.NumberFormat

/**
 * A full-screen page with a scrolling body. Pages you navigate to get a top bar with [title] and a
 * back arrow; root pages ([onBack] null) have none, because their body starts with its own title.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SpeecherScreen(
    title: String,
    onBack: (() -> Unit)?,
    actions: @Composable () -> Unit = {},
    content: @Composable () -> Unit,
) {
    Scaffold(
        topBar = {
            if (onBack != null) {
                TopAppBar(
                    title = { Text(title) },
                    navigationIcon = {
                        IconButton(onBack) {
                            Icon(
                                painterResource(R.drawable.ic_arrow_back),
                                contentDescription = "Back",
                            )
                        }
                    },
                    actions = { actions() },
                )
            }
        }
    ) { padding ->
        Column(
            // The keyboard shrinks the page rather than panning the window under the status bar.
            Modifier.fillMaxSize()
                .padding(padding)
                .consumeWindowInsets(padding)
                .imePadding()
                .verticalScroll(rememberScrollState())
                .padding(bottom = 24.dp)
        ) {
            content()
        }
    }
}

/**
 * The launcher's landing page: whether dictation is ready, a status row per thing it needs, any
 * update, and a field to try it in.
 */
@Composable
fun Home(
    status: SetupStatus,
    settings: SpeecherSettings,
    onOpenSetup: () -> Unit,
    onOpenSettings: () -> Unit,
    onSignIn: (Provider) -> Unit,
    onOpenChipSettings: () -> Unit,
    onOpenInsights: () -> Unit,
    modifier: Modifier = Modifier,
    update: UpdateState? = null,
    onUpdate: () -> Unit = {},
    latest: LatestTranscript? = null,
    onDismissUpdate: () -> Unit = {},
    whatsNewVersion: String? = null,
    onOpenWhatsNew: () -> Unit = {},
    onDismissWhatsNew: () -> Unit = {},
) {
    // What dictation will actually use: if the chosen provider isn't signed in, it falls back to
    // the connected account, so name that rather than the raw setting.
    val transcription = resolveSignedIn(settings.transcriptionProvider.account, status.signedIn)
    val refinement = resolveSignedIn(settings.refinementProvider.account, status.signedIn)
    Column(modifier) {
        Row(
            Modifier.padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            BrandTile(48.dp)
            Column(Modifier.padding(start = 16.dp)) {
                Text("Speecher", style = MaterialTheme.typography.titleLarge)
                Text(
                    when {
                        !status.complete -> "Finish setup to start dictating."
                        transcription in status.sessionEnded -> "Sign in again to start dictating."
                        else -> "Ready. Tap the dictation button on your keyboard to dictate."
                    },
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        // The update comes first, as on the desktop: What's New waits for it to go.
        if (update != null) UpdateCard(update, onUpdate, onDismissUpdate)
        else if (whatsNewVersion != null) {
            WhatsNewCard(whatsNewVersion, onOpenWhatsNew, onDismissWhatsNew)
        }
        ListItem(
            headlineContent = { Text("Settings") },
            supportingContent = {
                Text(
                    if (status.signedIn.isEmpty()) "Not signed in"
                    else
                        "Transcribing with ${transcription.accountLabel(status.sessionEnded)}" +
                            if (settings.refinementEnabled)
                                ", refining with ${refinement.accountLabel(status.sessionEnded)}"
                            else ""
                )
            },
            trailingContent = { Chevron() },
            modifier = Modifier.clickable(onClick = onOpenSettings),
        )
        ListItem(
            headlineContent = { Text("Insights") },
            supportingContent = {
                Text(if (settings.insightsEnabled) "Words, dictations and speaking pace" else "Off")
            },
            trailingContent = { Chevron() },
            modifier = Modifier.clickable(onClick = onOpenInsights),
        )
        if (latest != null) {
            Section("Latest transcript")
            ListItem(
                headlineContent = {
                    Text(latest.text, maxLines = 2, overflow = TextOverflow.Ellipsis)
                },
                supportingContent = { Text(latestTranscriptSummary(latest)) },
                trailingContent = { CopyTranscriptButton(latest.text) },
            )
        }
        Section("Status")
        AccountRow(status, onOpenSetup, onSignIn)
        StatusRow(
            "Microphone",
            if (status.microphoneGranted) "Allowed" else "Not allowed",
            status.microphoneGranted,
            onOpenSetup.takeIf { !status.microphoneGranted },
        )
        StatusRow(
            "Speecher keyboard",
            if (status.keyboardEnabled) "On" else "Off",
            status.keyboardEnabled,
            onOpenSetup.takeIf { !status.keyboardEnabled },
        )
        StatusRow(
            "Dictation button",
            if (status.chipEnabled) "On. Turn it off in Accessibility settings." else "Off",
            status.chipEnabled,
            if (status.chipEnabled) onOpenChipSettings else onOpenSetup,
        )
        Section("Try it")
        PracticeField(Modifier.padding(horizontal = 16.dp).fillMaxWidth())
    }
}

/** "12 words, Slack": the latest transcript's length and its app, as the desktop's Home puts it. */
internal fun latestTranscriptSummary(latest: LatestTranscript): String {
    val words = countWords(latest.text)
    val count =
        "${NumberFormat.getIntegerInstance().format(words)} ${if (words == 1) "word" else "words"}"
    return if (latest.app == null) count else "$count, ${latest.app}"
}

/** Signed in, signed out, or a session that ended, with the way to fix the last two. */
@Composable
private fun AccountRow(
    status: SetupStatus,
    onOpenSetup: () -> Unit,
    onSignIn: (Provider) -> Unit,
) {
    val ended = providerOrder.firstOrNull { it in status.signedIn && it in status.sessionEnded }
    when {
        status.signedIn.isEmpty() -> StatusRow("Account", "Not signed in", false, onOpenSetup)
        ended != null ->
            StatusRow("Account", "Your ${ended.label} session ended. Sign in again.", false) {
                onSignIn(ended)
            }
        else ->
            StatusRow(
                "Account",
                "Signed in to " +
                    providerOrder
                        .filter { it in status.signedIn }
                        .joinToString(" and ") { it.label },
                true,
                null,
            )
    }
}

/**
 * One thing dictation needs: a check when it's in place, otherwise an alert and the way to fix it.
 */
@Composable
private fun StatusRow(title: String, value: String, done: Boolean, onClick: (() -> Unit)?) {
    val colors = MaterialTheme.colorScheme
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = { Text(value) },
        leadingContent = {
            Icon(
                painterResource(if (done) R.drawable.ic_check else R.drawable.ic_error),
                contentDescription = null,
                tint = if (done) colors.primary else colors.error,
            )
        },
        trailingContent = onClick?.let { { Chevron() } },
        modifier = if (onClick == null) Modifier else Modifier.clickable(onClick = onClick),
    )
}

/** An update on offer, on its way in, or refused, with what can be done about it. */
@Composable
private fun UpdateCard(state: UpdateState, onUpdate: () -> Unit, onDismiss: () -> Unit) {
    val update = state.update ?: return
    HomeCard(
        when (state) {
            is UpdateState.Downloading -> downloadingText(state)
            is UpdateState.WaitingForDictation -> INSTALLING_AFTER_DICTATION
            is UpdateState.Installing -> installingText(state)
            else -> availableText(update)
        }
    ) {
        if (state is UpdateState.InstallFailed) {
            Text(
                state.message,
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.error,
            )
        }
        if (state is UpdateState.Downloading) {
            val progress = Modifier.fillMaxWidth().padding(top = 12.dp)
            state.percent?.let { LinearProgressIndicator({ it / 100f }, progress) }
                ?: LinearProgressIndicator(progress)
        }
        if (state is UpdateState.Available || state is UpdateState.InstallFailed) {
            CardActions(
                if (state is UpdateState.InstallFailed) retryCaption(state) else "Update",
                onUpdate,
                onDismiss,
            )
        }
    }
}

/** After an upgrade, until seen or dismissed, as the desktop's What's New banner. */
@Composable
private fun WhatsNewCard(installedVersion: String, onOpen: () -> Unit, onDismiss: () -> Unit) {
    // The bare number: a nightly's suffix says nothing to someone reading what changed.
    HomeCard("Speecher ${installedVersion.substringBefore('-')} is installed") {
        CardActions("See what's new", onOpen, onDismiss)
    }
}

@Composable
private fun HomeCard(title: String, content: @Composable ColumnScope.() -> Unit) {
    Card(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, bottom = 8.dp)) {
        Column(Modifier.fillMaxWidth().padding(16.dp)) {
            Text(title)
            content()
        }
    }
}

@Composable
private fun ColumnScope.CardActions(action: String, onAction: () -> Unit, onDismiss: () -> Unit) {
    Row(
        Modifier.align(Alignment.End).padding(top = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        TextButton(onDismiss) { Text("Dismiss") }
        Button(onAction) { Text(action) }
    }
}

@Composable
internal fun Chevron() {
    Icon(painterResource(R.drawable.ic_chevron_right), contentDescription = null)
}

@Composable
private fun HomePreview(status: SetupStatus, latest: LatestTranscript? = null) = SpeecherTheme {
    SpeecherScreen("", onBack = null) {
        Home(status, SpeecherSettings(), {}, {}, {}, {}, {}, latest = latest)
    }
}

@PreviewLightDark
@Composable
internal fun HomeReadyPreview() =
    HomePreview(SetupStatus(Provider.entries.toSet(), true, true, true))

@PreviewLightDark
@Composable
internal fun HomeSetupPendingPreview() =
    HomePreview(SetupStatus(setOf(Provider.Claude), true, false, false))

@PreviewLightDark
@Composable
internal fun HomeLatestTranscriptPreview() =
    HomePreview(
        SetupStatus(Provider.entries.toSet(), true, true, true),
        LatestTranscript(
            "Can we move the design review to Thursday afternoon? I'd like Priya to walk us " +
                "through the new onboarding flow before we lock it",
            "Slack",
        ),
    )

/** Home's top with [update] showing, or the What's New card for none. */
@Composable
private fun HomeUpdatePreview(update: UpdateState?) = SpeecherTheme {
    SpeecherScreen("", onBack = null) {
        Home(
            SetupStatus(Provider.entries.toSet(), true, true, true),
            SpeecherSettings(),
            {},
            {},
            {},
            {},
            {},
            update = update,
            whatsNewVersion = "0.3.0",
        )
    }
}

@PreviewLightDark
@Composable
internal fun HomeUpdateAvailablePreview() = HomeUpdatePreview(UpdateState.Available(previewUpdate))

@PreviewLightDark
@Composable
internal fun HomeUpdateDownloadingPreview() =
    HomeUpdatePreview(UpdateState.Downloading(previewUpdate, 42))

@PreviewLightDark
@Composable
internal fun HomeUpdateWaitingPreview() =
    HomeUpdatePreview(UpdateState.WaitingForDictation(previewUpdate))

@PreviewLightDark
@Composable
internal fun HomeUpdateInstallingPreview() =
    HomeUpdatePreview(UpdateState.Installing(previewUpdate))

@PreviewLightDark
@Composable
internal fun HomeUpdateStableReplacementPreview() =
    HomeUpdatePreview(
        UpdateState.Available(previewUpdate.copy(version = "0.2.0", replacesNightly = true))
    )

@PreviewLightDark
@Composable
internal fun HomeUpdateFailedPreview() =
    HomeUpdatePreview(installFailure(previewUpdate, PackageInstaller.STATUS_FAILURE_CONFLICT))

@PreviewLightDark @Composable internal fun HomeWhatsNewPreview() = HomeUpdatePreview(null)
