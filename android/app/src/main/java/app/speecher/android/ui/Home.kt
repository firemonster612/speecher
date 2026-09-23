package app.speecher.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
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
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SetupStatus
import app.speecher.android.dictation.SpeecherSettings
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

/** The launcher's landing page once setup is complete. */
@Composable
fun Home(
    status: SetupStatus,
    settings: SpeecherSettings,
    onOpenSetup: () -> Unit,
    onOpenSettings: () -> Unit,
    modifier: Modifier = Modifier,
    update: ApkUpdate? = null,
    onUpdate: () -> Unit = {},
    updateError: String? = null,
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
                    if (status.complete) "Ready. Tap the button beside your keyboard to dictate."
                    else "Setup isn't finished yet.",
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
        }
        if (!status.complete) {
            ListItem(
                headlineContent = { Text("Finish setup") },
                trailingContent = { Chevron() },
                modifier = Modifier.clickable(onClick = onOpenSetup),
            )
        }
        if (update != null)
            ListItem(
                headlineContent = { Text("Update to v${update.version}") },
                trailingContent = { Chevron() },
                modifier = Modifier.clickable(onClick = onUpdate),
            )
        updateError?.let {
            Text(it, Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.error)
        }
        ListItem(
            headlineContent = { Text("Settings") },
            supportingContent = {
                // Show what dictation will actually use: if the chosen provider isn't signed in,
                // it falls back to the connected account, so name that rather than the raw setting.
                val transcription =
                    resolveSignedIn(settings.transcriptionProvider, status.signedIn).label
                val refinement = resolveSignedIn(settings.refinementProvider, status.signedIn).label
                Text(
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

@Composable
private fun Chevron() {
    Icon(painterResource(R.drawable.ic_chevron_right), contentDescription = null)
}

@Composable
private fun HomePreview(status: SetupStatus) = SpeecherTheme {
    SpeecherScreen("", onBack = null) { Home(status, SpeecherSettings(), {}, {}) }
}

@PreviewLightDark
@Composable
internal fun HomeReadyPreview() =
    HomePreview(SetupStatus(Provider.entries.toSet(), true, true, true))

@PreviewLightDark
@Composable
internal fun HomeSetupPendingPreview() =
    HomePreview(SetupStatus(setOf(Provider.Claude), true, false, false))
