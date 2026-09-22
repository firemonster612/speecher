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

/**
 * A full-screen page: a top bar with an optional back arrow and a scrolling body. Every screen in
 * the app sits in one of these.
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
            TopAppBar(
                title = { Text(title) },
                navigationIcon = {
                    if (onBack != null) {
                        IconButton(onBack) {
                            Icon(
                                painterResource(R.drawable.ic_arrow_back),
                                contentDescription = "Back",
                            )
                        }
                    }
                },
                actions = { actions() },
            )
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
        ListItem(
            headlineContent = { Text("Settings") },
            supportingContent = {
                Text(
                    "Transcribing with ${settings.transcriptionProvider.label}" +
                        if (settings.refinementEnabled) {
                            ", refining with ${settings.refinementProvider.label}"
                        } else ""
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
    HomePreview(SetupStatus(Provider.entries.toSet(), true, true, true, true))

@PreviewLightDark
@Composable
internal fun HomeSetupPendingPreview() =
    HomePreview(SetupStatus(setOf(Provider.Claude), true, false, false, false))
