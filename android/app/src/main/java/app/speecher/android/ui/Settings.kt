package app.speecher.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuAnchorType
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.dictation.advice
import app.speecher.android.dictation.spokenLanguageLabel
import app.speecher.android.dictation.spokenLanguageMismatch
import app.speecher.android.dictation.spokenLanguageName
import app.speecher.android.dictation.spokenLanguages
import app.speecher.android.provider.OpenAiSpeed
import app.speecher.android.provider.Provider
import app.speecher.android.provider.RefinementChoice
import app.speecher.android.provider.SpeechProvider
import app.speecher.android.provider.cleanup
import app.speecher.android.provider.defaultRefinement
import app.speecher.android.provider.hasBatchTranscription
import app.speecher.android.provider.label
import app.speecher.android.provider.providerOrder
import app.speecher.android.provider.refinementEfforts
import app.speecher.android.provider.refinementModels
import app.speecher.android.provider.resolveSignedIn
import app.speecher.android.provider.resolveSpeech
import app.speecher.android.settings.SpeecherSettings
import app.speecher.android.update.UpdateState
import app.speecher.protocol.AUTOMATIC_LANGUAGE
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.SpeechEndpoint
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.modelSupportsFastMode
import app.speecher.protocol.modelSupportsUltrafast

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
private fun shownProvider(preferred: Provider, signedIn: Set<Provider>): Provider? =
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
private fun TranscriptionSettings(
    settings: SpeecherSettings,
    signedIn: Set<Provider>,
    onChange: (SpeecherSettings) -> Unit,
    onSignIn: (Provider) -> Unit,
) {
    // An account you aren't signed into isn't offered, since dictation would fall back to another.
    val provider = resolveSpeech(settings.transcriptionProvider, signedIn)
    val usable = SpeechProvider.entries.filter { it.account == null || it.account in signedIn }
    DropdownRow(
        "Provider",
        usable.associateWith { it.label },
        provider,
        selectedLabel = if (provider in usable) provider.label else "Not signed in",
    ) {
        onChange(settings.copy(transcriptionProvider = it))
    }
    SignInLinks(signedIn, onSignIn, Modifier.padding(horizontal = 16.dp))
    DropdownRow(
        "Spoken language",
        spokenLanguageChoices(provider),
        settings.spokenLanguage,
        description = "The language you dictate in.",
        selectedLabel = spokenLanguageLabel(settings.spokenLanguage),
    ) {
        onChange(settings.copy(spokenLanguage = it))
    }
    spokenLanguageMismatch(provider, settings.spokenLanguage)?.let {
        Text(
            it.advice,
            Modifier.padding(horizontal = 16.dp),
            color = MaterialTheme.colorScheme.error,
        )
    }
    ListItem(
        headlineContent = { Text("Keep screen on") },
        supportingContent = { Text("Stops the screen turning off while you dictate.") },
        trailingContent = {
            Switch(settings.keepScreenOn, { onChange(settings.copy(keepScreenOn = it)) })
        },
        colors = rowColors(),
    )
    if (provider.hasBatchTranscription) {
        ListItem(
            headlineContent = { Text("Extra transcription pass") },
            supportingContent = {
                Text(
                    "Transcribes the whole recording again before inserting. More accurate, slower."
                )
            },
            trailingContent = {
                Switch(
                    settings.transcribePassEnabled,
                    { onChange(settings.copy(transcribePassEnabled = it)) },
                )
            },
            colors = rowColors(),
        )
    }
    if (provider == SpeechProvider.Endpoint) SpeechEndpointSettings(settings, onChange)
    Section("Microphone")
    MicrophoneTestRow()
}

@Composable
private fun RefinementSettings(
    settings: SpeecherSettings,
    signedIn: Set<Provider>,
    onChange: (SpeecherSettings) -> Unit,
    onSignIn: (Provider) -> Unit,
) {
    ListItem(
        headlineContent = { Text("Offer Insert refined") },
        supportingContent = { Text("Cleans up filler words and punctuation before inserting.") },
        trailingContent = {
            Switch(settings.refinementEnabled, { onChange(settings.copy(refinementEnabled = it)) })
        },
        colors = rowColors(),
    )
    if (!settings.refinementEnabled) return
    ProviderPicker("Provider", settings.refinementProvider.account, signedIn, onSignIn) {
        onChange(settings.copy(refinementProvider = it.cleanup))
    }
    val provider = resolveSignedIn(settings.refinementProvider.account, signedIn)
    val choice = settings.refinement(provider)
    key(provider) {
        ModelField(provider.refinementModels, choice.model, provider.defaultRefinement.model) {
            onChange(settings.withRefinement(provider, choice.copy(model = it)))
        }
    }
    DropdownRow(
        "Thinking",
        provider.refinementEfforts,
        choice.effort,
        description = "More thinking follows instructions more closely but takes longer.",
    ) {
        onChange(settings.withRefinement(provider, choice.copy(effort = it)))
    }
    if (provider == Provider.ChatGpt) {
        val ultrafast = modelSupportsUltrafast(choice.model)
        val speeds = OpenAiSpeed.entries.filter { ultrafast || it != OpenAiSpeed.Ultrafast }
        DropdownRow(
            "Speed",
            speeds.associateWith { it.label },
            settings.chatGptSpeed,
            description =
                if (ultrafast) "Faster tiers use more of your plan."
                else
                    "Faster tiers use more of your plan. Ultrafast needs GPT-6 Astra or GPT-6.1 Sol.",
        ) {
            onChange(settings.copy(chatGptSpeed = it))
        }
        // Other plans take an Ultrafast request at Standard without complaint.
        if (ultrafast && settings.chatGptSpeed == OpenAiSpeed.Ultrafast) {
            Text(
                "Ultrafast only works on ChatGPT Pro \$500 and eligible Enterprise and Edu plans. " +
                    "Other plans run it at Standard, which is slower than Fast.",
                Modifier.padding(horizontal = 16.dp),
                color = MaterialTheme.colorScheme.error,
            )
        }
    } else {
        ListItem(
            headlineContent = { Text("Fast mode") },
            supportingContent = {
                Text(
                    if (modelSupportsFastMode(choice.model))
                        "Faster refinement for a little more usage."
                    else "Only works with Opus models."
                )
            },
            trailingContent = {
                Switch(settings.claudeFastMode, { onChange(settings.copy(claudeFastMode = it)) })
            },
            colors = rowColors(),
        )
    }
    DropdownRow(
        "Fallback profile",
        profileChoices(settings),
        settings.defaultWritingProfile,
        description = "Used when the app doesn't suggest a profile.",
    ) {
        onChange(settings.copy(defaultWritingProfile = it))
    }
    InstructionsField(
        "Additional instructions",
        "Added to every refinement, before each profile's own instructions.",
        settings.additionalInstructions,
    ) {
        onChange(settings.copy(additionalInstructions = it))
    }
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

@Composable
private fun SwitchRow(
    title: String,
    checked: Boolean,
    description: String? = null,
    onCheckedChange: (Boolean) -> Unit,
) {
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = description?.let { { Text(it) } },
        trailingContent = { Switch(checked, onCheckedChange) },
        colors = rowColors(),
    )
}

/**
 * The two providers as segments under [title]. One you aren't signed into can't be used, since
 * dictation would silently fall back to the other account, so it is disabled with a sign-in link
 * under it, and the check sits on the provider dictation will actually use.
 */
@Composable
private fun ProviderPicker(
    title: String,
    selected: Provider,
    signedIn: Set<Provider>,
    onSignIn: (Provider) -> Unit,
    onSelect: (Provider) -> Unit,
) {
    val shown = shownProvider(selected, signedIn)
    Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        Text(title, style = MaterialTheme.typography.bodyLarge)
        SingleChoiceSegmentedButtonRow(Modifier.fillMaxWidth().padding(top = 8.dp)) {
            providerOrder.forEachIndexed { index, provider ->
                SegmentedButton(
                    selected = provider == shown,
                    onClick = { onSelect(provider) },
                    enabled = provider in signedIn,
                    shape = SegmentedButtonDefaults.itemShape(index, providerOrder.size),
                ) {
                    Text(provider.label, maxLines = 1)
                }
            }
        }
        SignInLinks(signedIn, onSignIn)
    }
}

/** A link to sign in to each account that isn't, so a provider picker can offer it. */
@Composable
private fun SignInLinks(
    signedIn: Set<Provider>,
    onSignIn: (Provider) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier) {
        providerOrder
            .filter { it !in signedIn }
            .forEach { provider ->
                TextButton({ onSignIn(provider) }, Modifier.offset(x = (-12).dp)) {
                    Text("Sign in to ${provider.label} to use it")
                }
            }
    }
}

/**
 * The refinement model as free text whose menu suggests [models], as the desktop's editable combo
 * does: a suggested model shows by name and saves as its id, any other id as typed. A cleared field
 * saves [default], since every request needs a model, and shows it once the field is left.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun ModelField(
    models: Map<String, String>,
    model: String,
    default: String,
    onChange: (String) -> Unit,
) {
    val saved = models[model] ?: model
    var text by rememberSaveable { mutableStateOf(saved) }
    var expanded by remember { mutableStateOf(false) }
    val focus = LocalFocusManager.current
    fun edit(value: String) {
        text = value
        val typed = value.trim()
        onChange(
            if (typed.isEmpty()) default
            else models.entries.firstOrNull { it.value == typed }?.key ?: typed
        )
    }
    ExposedDropdownMenuBox(
        expanded,
        { expanded = it },
        Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
    ) {
        OutlinedTextField(
            text,
            ::edit,
            Modifier.fillMaxWidth()
                .menuAnchor(ExposedDropdownMenuAnchorType.PrimaryEditable)
                .onFocusChanged { if (!it.isFocused) text = saved },
            label = { Text("Model") },
            supportingText = { Text("Select a model or type another model ID.") },
            trailingIcon = { ExposedDropdownMenuDefaults.TrailingIcon(expanded) },
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
            keyboardActions =
                KeyboardActions(
                    onDone = {
                        expanded = false
                        focus.clearFocus()
                    }
                ),
            singleLine = true,
        )
        ExposedDropdownMenu(expanded, { expanded = false }) {
            models.values.forEach { label ->
                DropdownMenuItem(
                    text = { Text(label) },
                    onClick = {
                        expanded = false
                        edit(label)
                    },
                )
            }
        }
    }
}

/** Several lines of instructions for the refiner, under a title and what they are for. */
@Composable
private fun InstructionsField(
    title: String,
    description: String,
    value: String,
    onValueChange: (String) -> Unit,
) {
    Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        Text(title, style = MaterialTheme.typography.bodyLarge)
        Text(
            description,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        OutlinedTextField(value, onValueChange, Modifier.fillMaxWidth(), minLines = 3)
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

/** The provider's spoken languages as code to label: Automatic, then by English name. */
private fun spokenLanguageChoices(provider: SpeechProvider): Map<String, String> =
    provider.spokenLanguages
        .sortedWith(compareBy({ it != AUTOMATIC_LANGUAGE }, ::spokenLanguageName))
        .associateWith(::spokenLanguageLabel)

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
internal fun SettingsTranscriptionServerPreview() = SpeecherTheme {
    var settings by remember { mutableStateOf(previewServerSettings) }
    SpeecherScreen(SettingsPage.Transcription.title, onBack = {}) {
        SettingsPageContent(
            SettingsPage.Transcription,
            settings,
            emptySet(),
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
internal fun SettingsRefinementPreview() = SpeecherTheme {
    Surface {
        Column {
            SettingsPageContent(
                SettingsPage.Refinement,
                SpeecherSettings(),
                setOf(Provider.ChatGpt),
                {},
                {},
                {},
                chipEnabled = true,
                onTurnOnChip = {},
            )
        }
    }
}

@PreviewLightDark
@Composable
internal fun SettingsRefinementUltrafastPreview() = SpeecherTheme {
    Surface {
        Column {
            SettingsPageContent(
                SettingsPage.Refinement,
                SpeecherSettings(
                    chatGptRefinement = RefinementChoice("gpt-6.1-sol", "low"),
                    chatGptSpeed = OpenAiSpeed.Ultrafast,
                ),
                setOf(Provider.ChatGpt),
                {},
                {},
                {},
                chipEnabled = true,
                onTurnOnChip = {},
            )
        }
    }
}

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
