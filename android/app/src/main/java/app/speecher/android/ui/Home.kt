package app.speecher.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SetupStatus
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.label
import app.speecher.android.dictation.providerOrder
import app.speecher.android.dictation.resolveSignedIn
import app.speecher.android.update.ApkUpdate

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
            Modifier.fillMaxSize()
                .padding(padding)
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
    modifier: Modifier = Modifier,
    update: ApkUpdate? = null,
    updating: Boolean = false,
    updateFailed: Boolean = false,
    onUpdate: () -> Unit = {},
) {
    Column(modifier) {
        Row(
            Modifier.padding(start = 16.dp, end = 16.dp, top = 8.dp, bottom = 16.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            BrandTile(48.dp)
            Column(Modifier.padding(start = 16.dp)) {
                Text("Speecher", style = MaterialTheme.typography.titleLarge)
                Text(
                    if (status.complete)
                        "Ready. Tap the dictation button on your keyboard to dictate."
                    else "Finish setup to start dictating.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        if (update != null) UpdateCard(update, updating, updateFailed, onUpdate)
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
        ListItem(
            headlineContent = { Text("Settings") },
            supportingContent = {
                // Show what dictation will actually use: if the chosen provider isn't signed in,
                // it falls back to the connected account, so name that rather than the raw setting.
                val transcription =
                    resolveSignedIn(settings.transcriptionProvider, status.signedIn).label
                val refinement = resolveSignedIn(settings.refinementProvider, status.signedIn).label
                Text(
                    if (status.signedIn.isEmpty()) "Not signed in"
                    else
                        "Transcribing with $transcription" +
                            if (settings.refinementEnabled) ", refining with $refinement" else ""
                )
            },
            trailingContent = { Chevron() },
            modifier = Modifier.clickable(onClick = onOpenSettings),
        )
        Section("Try it")
        PracticeField(Modifier.padding(horizontal = 16.dp).fillMaxWidth())
    }
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

@Composable
private fun UpdateCard(
    update: ApkUpdate,
    updating: Boolean,
    failed: Boolean,
    onUpdate: () -> Unit,
) {
    Card(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, bottom = 8.dp)) {
        Row(Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f).padding(end = 16.dp)) {
                Text("Version ${update.version} is available")
                if (failed) {
                    Text(
                        "Couldn't download the update. Check your connection and try again.",
                        style = MaterialTheme.typography.bodyMedium,
                        color = MaterialTheme.colorScheme.error,
                    )
                }
            }
            if (updating) {
                CircularProgressIndicator(
                    Modifier.size(24.dp).semantics { contentDescription = "Downloading update" }
                )
            } else {
                Button(onUpdate) { Text(if (failed) "Retry" else "Update") }
            }
        }
    }
}

@Composable
internal fun Chevron() {
    Icon(painterResource(R.drawable.ic_chevron_right), contentDescription = null)
}

@Composable
private fun HomePreview(status: SetupStatus) = SpeecherTheme {
    SpeecherScreen("", onBack = null) { Home(status, SpeecherSettings(), {}, {}, {}, {}) }
}

@PreviewLightDark
@Composable
internal fun HomeReadyPreview() =
    HomePreview(SetupStatus(Provider.entries.toSet(), true, true, true))

@PreviewLightDark
@Composable
internal fun HomeSetupPendingPreview() =
    HomePreview(SetupStatus(setOf(Provider.Claude), true, false, false))
