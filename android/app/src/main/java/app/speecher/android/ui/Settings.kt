package app.speecher.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Card
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.provider.Provider
import app.speecher.android.provider.SpeechProvider
import app.speecher.android.provider.label
import app.speecher.android.provider.providerOrder
import app.speecher.android.provider.refinementModels
import app.speecher.android.provider.resolveSignedIn
import app.speecher.android.settings.SpeecherSettings
import app.speecher.android.update.UpdateState
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.SpeechEndpoint
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings

/** The pages the Settings list opens, each under its own top bar with a back arrow. */
enum class SettingsPage(val title: String) {
    Transcription("Transcription"),
    Refinement("Refinement"),
    RefinementContext("What refinement can read"),
    DictationPanel("Dictation panel"),
    Vocabulary("Vocabulary"),
    Replacements("Replacements & snippets"),
    LearnedCorrections("Learned corrections"),
    Profiles("Profiles"),
    AppRules("Application rules"),
    Tones("Tones"),
    CleanupLevels("Cleanup levels"),
    CustomPrompt("Custom prompt"),
}

/**
 * The Settings list: accounts first, since nothing works without one, then a row per page with its
 * current value. The writing pages only matter with refinement on, so they show only then.
 */
@Composable
fun Settings(
    settings: SpeecherSettings,
    signedIn: Set<Provider>,
    onOpen: (SettingsPage) -> Unit,
    onSignIn: (Provider) -> Unit,
    onSignOut: (Provider) -> Unit,
    onChange: (SpeecherSettings) -> Unit,
    modifier: Modifier = Modifier,
    sessionEnded: Set<Provider> = emptySet(),
    signingIn: Provider? = null,
    signInError: String? = null,
    onPasteCode: (String) -> Unit = {},
    onRunSetup: () -> Unit = {},
    onImportFromComputer: () -> Unit = {},
    updateState: UpdateState = UpdateState.Idle,
    onCheckForUpdates: () -> Unit = {},
    onOpenWhatsNew: () -> Unit = {},
) {
    Column(modifier) {
        if (signedIn.isEmpty() && !settings.transcribesWithServer) {
            Card(Modifier.fillMaxWidth().padding(start = 16.dp, end = 16.dp, top = 16.dp)) {
                Text(
                    "Sign in to start dictating. Speecher uses your own ChatGPT or Claude account.",
                    Modifier.padding(16.dp),
                    style = MaterialTheme.typography.bodyLarge,
                )
            }
        }
        Section("Accounts")
        signInError?.let {
            Text(it, Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.error)
        }
        providerOrder.forEach { provider ->
            val isSignedIn = provider in signedIn
            val ended = isSignedIn && provider in sessionEnded
            ListItem(
                headlineContent = { Text(provider.label) },
                supportingContent = {
                    Text(
                        when {
                            ended -> "Session ended"
                            isSignedIn -> "Signed in"
                            else -> "Signed out"
                        }
                    )
                },
                trailingContent = {
                    Row {
                        if (ended) TextButton({ onSignIn(provider) }) { Text("Sign in again") }
                        if (isSignedIn) TextButton({ onSignOut(provider) }) { Text("Sign out") }
                        else TextButton({ onSignIn(provider) }) { Text("Sign in") }
                    }
                },
                colors = rowColors(),
            )
        }
        signingIn?.let { PasteCode(it, onPasteCode, Modifier.padding(horizontal = 16.dp)) }

        Section("Dictation")
        PageRow(
            SettingsPage.Transcription,
            transcriptionSummary(settings, signedIn, sessionEnded),
            onOpen,
        )
        PageRow(
            SettingsPage.Refinement,
            refinementSummary(settings, signedIn, sessionEnded),
            onOpen,
        )
        if (settings.refinementEnabled) {
            PageRow(SettingsPage.RefinementContext, contextSummary(settings), onOpen)
        }
        PageRow(SettingsPage.DictationPanel, panelSummary(settings), onOpen)
        PageRow(
            SettingsPage.Vocabulary,
            if (settings.vocabulary.isEmpty()) "No words"
            else count(settings.vocabulary.size, "word", "words"),
            onOpen,
        )
        PageRow(
            SettingsPage.Replacements,
            if (settings.replacements.isEmpty()) "No replacements or snippets"
            else count(settings.replacements.size, "replacement", "replacements"),
            onOpen,
        )
        PageRow(SettingsPage.LearnedCorrections, correctionsSummary(settings), onOpen)

        if (settings.refinementEnabled) {
            Section("Advanced")
            PageRow(
                SettingsPage.Profiles,
                count(profileChoices(settings).size, "profile", "profiles"),
                onOpen,
            )
            PageRow(
                SettingsPage.AppRules,
                if (settings.appRules.isEmpty()) "Built-in rules only"
                else count(settings.appRules.size, "rule of your own", "rules of your own"),
                onOpen,
            )
            PageRow(
                SettingsPage.Tones,
                if (settings.customTones.isEmpty()) "Built-in tones only"
                else count(settings.customTones.size, "custom tone", "custom tones"),
                onOpen,
            )
            PageRow(
                SettingsPage.CleanupLevels,
                if (settings.customCleanupLevels.isEmpty()) "Built-in levels only"
                else count(settings.customCleanupLevels.size, "custom level", "custom levels"),
                onOpen,
            )
            PageRow(
                SettingsPage.CustomPrompt,
                if (settings.customSystemPromptEnabled) "On" else "Off",
                onOpen,
            )
        }

        Section("App")
        ListItem(
            headlineContent = { Text("Setup assistant") },
            supportingContent = { Text("Go through the first-run steps again.") },
            trailingContent = { Chevron() },
            modifier = Modifier.clickable(onClick = onRunSetup),
            colors = rowColors(),
        )
        ListItem(
            headlineContent = { Text("Import from computer") },
            supportingContent = {
                Text("Scan the code Speecher shows under General on your computer.")
            },
            trailingContent = { Chevron() },
            modifier = Modifier.clickable(onClick = onImportFromComputer),
            colors = rowColors(),
        )

        UpdatesSection(settings, onChange, updateState, onCheckForUpdates, onOpenWhatsNew)
    }
}

/**
 * One page from the Settings list. Every change goes out whole through [onChange]; the caller
 * persists it.
 */
@Composable
fun SettingsPageContent(
    page: SettingsPage,
    settings: SpeecherSettings,
    signedIn: Set<Provider>,
    onChange: (SpeecherSettings) -> Unit,
    onSignIn: (Provider) -> Unit,
    onSetChipPosition: () -> Unit,
    chipEnabled: Boolean,
    onTurnOnChip: () -> Unit,
) {
    when (page) {
        SettingsPage.Transcription -> TranscriptionSettings(settings, signedIn, onChange, onSignIn)
        SettingsPage.Refinement -> RefinementSettings(settings, signedIn, onChange, onSignIn)
        SettingsPage.RefinementContext -> RefinementContextSettings(settings, onChange)
        SettingsPage.DictationPanel -> DictationPanelSettings(settings, onChange, onSetChipPosition)
        SettingsPage.Vocabulary -> VocabularySettings(settings, onChange)
        SettingsPage.Replacements -> ReplacementSettings(settings, onChange)
        SettingsPage.LearnedCorrections ->
            CorrectionSettings(settings, chipEnabled, onTurnOnChip, onChange)
        SettingsPage.Profiles -> ProfileSettings(settings, onChange)
        SettingsPage.AppRules -> AppRuleSettings(settings, onChange)
        SettingsPage.Tones -> ToneSettings(settings, onChange)
        SettingsPage.CleanupLevels -> CleanupLevelSettings(settings, onChange)
        SettingsPage.CustomPrompt -> CustomPromptSettings(settings, onChange)
    }
}

internal fun count(n: Int, one: String, many: String) = if (n == 1) "1 $one" else "$n $many"

private fun correctionsSummary(settings: SpeecherSettings): String {
    val corrections = settings.learnedCorrections
    val learned =
        if (corrections.isEmpty()) "No corrections"
        else count(corrections.size, "correction", "corrections")
    return if (settings.correctionLearningEnabled) learned else "$learned, learning off"
}

/** The provider dictation will use, as the pickers show it, or null with no account. */
internal fun shownProvider(preferred: Provider, signedIn: Set<Provider>): Provider? =
    resolveSignedIn(preferred, signedIn).takeIf { it in signedIn }

/** The provider's name, marked when its session ended, for the summaries that name an account. */
internal fun Provider.accountLabel(sessionEnded: Set<Provider>): String =
    if (this in sessionEnded) "$label (session ended)" else label

private fun transcriptionSummary(
    settings: SpeecherSettings,
    signedIn: Set<Provider>,
    sessionEnded: Set<Provider>,
): String {
    val account = settings.transcriptionProvider.account ?: return SpeechProvider.Endpoint.label
    return shownProvider(account, signedIn)?.accountLabel(sessionEnded) ?: "Not signed in"
}

private fun refinementSummary(
    settings: SpeecherSettings,
    signedIn: Set<Provider>,
    sessionEnded: Set<Provider>,
): String {
    if (!settings.refinementEnabled) return "Off"
    val provider =
        shownProvider(settings.refinementProvider.account, signedIn) ?: return "Not signed in"
    val model = settings.refinement(provider).model
    return "${provider.accountLabel(sessionEnded)}, ${provider.refinementModels[model] ?: model}"
}

private fun contextSummary(settings: SpeecherSettings): String {
    if (!settings.useTargetContext) return "Only the app's name"
    return listOfNotNull(
            "text around the cursor",
            "screen text".takeIf { settings.includeScreenText },
            "screenshot".takeIf { settings.includeScreenshot },
        )
        .joinToString(", ")
        .replaceFirstChar(Char::uppercase)
}

private fun panelSummary(settings: SpeecherSettings): String =
    "${settings.panelSize.label} panel" +
        if (settings.refinementEnabled) ", ${settings.buttonLayout.label}" else ""

/** A row that opens [page], showing its current value. */
@Composable
private fun PageRow(page: SettingsPage, summary: String, onOpen: (SettingsPage) -> Unit) {
    ListItem(
        headlineContent = { Text(page.title) },
        supportingContent = { Text(summary) },
        trailingContent = { Chevron() },
        modifier = Modifier.clickable { onOpen(page) },
        colors = rowColors(),
    )
}

@Composable
private fun RefinementContextSettings(
    settings: SpeecherSettings,
    onChange: (SpeecherSettings) -> Unit,
) {
    Text(
        "Refinement always gets the app's name, to pick a writing profile.",
        Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    ListItem(
        headlineContent = { Text("Text around the cursor") },
        supportingContent = { Text("Sends the field's placeholder and the text near the cursor.") },
        trailingContent = {
            Switch(settings.useTargetContext, { onChange(settings.copy(useTargetContext = it)) })
        },
        colors = rowColors(),
    )
    if (!settings.useTargetContext) return
    ListItem(
        headlineContent = { Text("Screen text") },
        supportingContent = { Text("Sends the visible text of the app you're dictating into.") },
        trailingContent = {
            Switch(settings.includeScreenText, { onChange(settings.copy(includeScreenText = it)) })
        },
        colors = rowColors(),
    )
    ListItem(
        headlineContent = { Text("Screenshot") },
        supportingContent = { Text("Sends a picture of that app. Needs a vision model.") },
        trailingContent = {
            Switch(settings.includeScreenshot, { onChange(settings.copy(includeScreenshot = it)) })
        },
        colors = rowColors(),
    )
}

@Composable
private fun DictationPanelSettings(
    settings: SpeecherSettings,
    onChange: (SpeecherSettings) -> Unit,
    onSetChipPosition: () -> Unit,
) {
    // With refinement off the panel only offers Insert, so there is no layout to choose.
    if (settings.refinementEnabled) {
        Section("Insert buttons")
        ButtonLayoutPicker(settings.buttonLayout) { onChange(settings.copy(buttonLayout = it)) }
    }
    Section("Panel size")
    PanelSizePicker(settings.panelSize) { onChange(settings.copy(panelSize = it)) }

    Section("Dictation button")
    ListItem(
        headlineContent = { Text("Dock on the keyboard's mic key") },
        supportingContent = { Text("Dragging the button moves it until the keyboard closes.") },
        trailingContent = {
            Switch(settings.chipDockOnMic, { onChange(settings.copy(chipDockOnMic = it)) })
        },
        colors = rowColors(),
    )
    if (!settings.chipDockOnMic) {
        ListItem(
            headlineContent = { Text("Button position") },
            supportingContent = { Text(if (settings.chipOffsetX == null) "Default" else "Saved") },
            trailingContent = { Chevron() },
            modifier = Modifier.clickable(onClick = onSetChipPosition),
            colors = rowColors(),
        )
    }

    Section("While dictating")
    SwitchRow(
        "Pause media while dictating",
        settings.pauseMedia,
        "Asks apps playing audio to pause until you insert or cancel.",
    ) {
        onChange(settings.copy(pauseMedia = it))
    }
    SwitchRow(
        "Vibrate when dictation starts and stops",
        settings.vibrationEnabled,
        "Follows your phone's touch feedback setting.",
    ) {
        onChange(settings.copy(vibrationEnabled = it))
    }
    SwitchRow("Show live text while you speak", settings.transcriptionPreviewEnabled) {
        onChange(settings.copy(transcriptionPreviewEnabled = it))
    }
    if (settings.refinementEnabled) {
        SwitchRow("Show live text during refinement", settings.refinementPreviewEnabled) {
            onChange(settings.copy(refinementPreviewEnabled = it))
        }
    }
}

/**
 * Shown the whole time a sign-in waits, so anyone back from a browser that didn't return them finds
 * the fallback without looking for it.
 */
@Composable
internal fun PasteCode(
    provider: Provider,
    onPasteCode: (String) -> Unit,
    modifier: Modifier = Modifier,
) {
    var code by rememberSaveable { mutableStateOf("") }
    Column(modifier) {
        Text(
            "Waiting for ${provider.label}. If the browser didn't bring you back, copy its " +
                "address bar (it starts with http://localhost) and paste it here.",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                code,
                { code = it },
                Modifier.weight(1f),
                label = { Text("Pasted link or code") },
                singleLine = true,
            )
            TextButton({ onPasteCode(code) }, enabled = code.isNotBlank()) { Text("Continue") }
        }
    }
}

@Composable
private fun SettingsPreview(settings: SpeecherSettings, signedIn: Set<Provider>) = SpeecherTheme {
    Surface { Settings(settings, signedIn, {}, {}, {}, {}) }
}

@PreviewLightDark
@Composable
internal fun SettingsPreview() =
    SettingsPreview(
        SpeecherSettings(
            vocabulary = listOf("Speecher", "Kirigami", "Priya Raman").map(::VocabularyWord)
        ),
        Provider.entries.toSet(),
    )

/** The list under its top bar and scrolling, as the app shows it, so its last rows can be seen. */
@PreviewLightDark
@Composable
internal fun SettingsScreenPreview() = SpeecherTheme {
    SpeecherScreen("Settings", onBack = {}) {
        Settings(SpeecherSettings(), Provider.entries.toSet(), {}, {}, {}, {})
    }
}

@PreviewLightDark
@Composable
internal fun SettingsSignedOutPreview() = SettingsPreview(SpeecherSettings(), emptySet())

/** Transcribing with a server on the home network, no account. */
internal val previewServerSettings =
    SpeecherSettings(
        transcriptionProvider = SpeechProvider.Endpoint,
        speechEndpoint = SpeechEndpoint("http://192.168.1.20:8080", "/inference", "whisper-1"),
    )

@PreviewLightDark
@Composable
internal fun SettingsServerPreview() = SettingsPreview(previewServerSettings, emptySet())

@PreviewLightDark
@Composable
internal fun SettingsDictationPanelPreview() = SpeecherTheme {
    var settings by remember { mutableStateOf(SpeecherSettings()) }
    SpeecherScreen(SettingsPage.DictationPanel.title, onBack = {}) {
        SettingsPageContent(
            SettingsPage.DictationPanel,
            settings,
            Provider.entries.toSet(),
            { settings = it },
            {},
            {},
            chipEnabled = true,
            onTurnOnChip = {},
        )
    }
}

@PreviewLightDark
@Composable
internal fun SettingsProfilesPreview() = SpeecherTheme {
    var settings by remember {
        mutableStateOf(
            SpeecherSettings(
                writingProfiles =
                    SpeecherSettings().writingProfiles +
                        (WritingProfile.Work to
                            WritingProfileSettings(
                                CleanupStrength.None,
                                outputLanguage = "Spanish",
                            ))
            )
        )
    }
    SpeecherScreen(SettingsPage.Profiles.title, onBack = {}) {
        SettingsPageContent(
            SettingsPage.Profiles,
            settings,
            Provider.entries.toSet(),
            { settings = it },
            {},
            {},
            chipEnabled = true,
            onTurnOnChip = {},
        )
    }
}
