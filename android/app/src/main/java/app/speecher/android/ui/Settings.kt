package app.speecher.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.selection.toggleable
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.ExposedDropdownMenuAnchorType
import androidx.compose.material3.ExposedDropdownMenuBox
import androidx.compose.material3.ExposedDropdownMenuDefaults
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemColors
import androidx.compose.material3.ListItemDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.PlainTooltip
import androidx.compose.material3.RadioButton
import androidx.compose.material3.SegmentedButton
import androidx.compose.material3.SegmentedButtonDefaults
import androidx.compose.material3.SingleChoiceSegmentedButtonRow
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TooltipAnchorPosition
import androidx.compose.material3.TooltipBox
import androidx.compose.material3.TooltipDefaults
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.material3.rememberTooltipState
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
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.OpenAiSpeed
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.RefinementChoice
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.defaultRefinement
import app.speecher.android.dictation.hasBatchTranscription
import app.speecher.android.dictation.label
import app.speecher.android.dictation.providerOrder
import app.speecher.android.dictation.refinementEfforts
import app.speecher.android.dictation.refinementModelCaution
import app.speecher.android.dictation.refinementModels
import app.speecher.android.dictation.resolveSignedIn
import app.speecher.android.dictation.spokenLanguageLabel
import app.speecher.android.dictation.spokenLanguageMismatch
import app.speecher.android.dictation.spokenLanguageName
import app.speecher.android.dictation.spokenLanguages
import app.speecher.protocol.AUTOMATIC_LANGUAGE
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.MAX_REFINEMENT_TERMS
import app.speecher.protocol.VocabularyWord
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.claudeVoiceKeytermIndices
import app.speecher.protocol.claudeVoiceKeyterms
import app.speecher.protocol.modelSupportsFastMode
import app.speecher.protocol.modelSupportsUltrafast
import app.speecher.protocol.speechTerms

/** The pages the Settings list opens, each under its own top bar with a back arrow. */
enum class SettingsPage(val title: String) {
    Transcription("Transcription"),
    Refinement("Refinement"),
    RefinementContext("What refinement can read"),
    DictationPanel("Dictation panel"),
    Vocabulary("Vocabulary"),
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
) {
    Column(modifier) {
        if (signedIn.isEmpty()) {
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

        Section("Updates")
        UpdateCheckRow(settings, onChange)
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
) {
    when (page) {
        SettingsPage.Transcription -> TranscriptionSettings(settings, signedIn, onChange, onSignIn)
        SettingsPage.Refinement -> RefinementSettings(settings, signedIn, onChange, onSignIn)
        SettingsPage.RefinementContext -> RefinementContextSettings(settings, onChange)
        SettingsPage.DictationPanel -> DictationPanelSettings(settings, onChange, onSetChipPosition)
        SettingsPage.Vocabulary -> VocabularySettings(settings, onChange)
        SettingsPage.Profiles -> ProfileSettings(settings, onChange)
        SettingsPage.AppRules -> AppRuleSettings(settings, onChange)
        SettingsPage.Tones -> ToneSettings(settings, onChange)
        SettingsPage.CleanupLevels -> CleanupLevelSettings(settings, onChange)
        SettingsPage.CustomPrompt -> CustomPromptSettings(settings, onChange)
    }
}

private fun count(n: Int, one: String, many: String) = if (n == 1) "1 $one" else "$n $many"

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
): String =
    shownProvider(settings.transcriptionProvider, signedIn)?.accountLabel(sessionEnded)
        ?: "Not signed in"

private fun refinementSummary(
    settings: SpeecherSettings,
    signedIn: Set<Provider>,
    sessionEnded: Set<Provider>,
): String {
    if (!settings.refinementEnabled) return "Off"
    val provider = shownProvider(settings.refinementProvider, signedIn) ?: return "Not signed in"
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

@Composable
internal fun rowColors(): ListItemColors =
    ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.surface)

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
    ProviderPicker("Provider", settings.transcriptionProvider, signedIn, onSignIn) {
        onChange(settings.copy(transcriptionProvider = it))
    }
    val provider = resolveSignedIn(settings.transcriptionProvider, signedIn)
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
        Text(it, Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.error)
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
    ProviderPicker("Provider", settings.refinementProvider, signedIn, onSignIn) {
        onChange(settings.copy(refinementProvider = it))
    }
    val provider = resolveSignedIn(settings.refinementProvider, signedIn)
    val choice = settings.refinement(provider)
    key(provider) {
        ModelField(provider.refinementModels, choice.model, provider.defaultRefinement.model) {
            onChange(settings.withRefinement(provider, choice.copy(model = it)))
        }
    }
    refinementModelCaution(provider, choice.model)?.let {
        Text(it, Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.error)
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
                if (ultrafast)
                    "Faster tiers use more of your plan. Ultrafast needs a plan that includes it."
                else "Faster tiers use more of your plan. Ultrafast needs GPT-6 Astra.",
        ) {
            onChange(settings.copy(chatGptSpeed = it))
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
}

@Composable
private fun VocabularySettings(settings: SpeecherSettings, onChange: (SpeecherSettings) -> Unit) {
    Text(
        vocabularySummary(settings),
        Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
    val keyTerms = keyTerms(settings)
    val profiles = profileChoices(settings)
    // The word open in the editor; one not in the list yet is being added.
    var editing by remember { mutableStateOf<VocabularyWord?>(null) }
    settings.vocabulary.forEach { word ->
        ListItem(
            headlineContent = { Text(word.term) },
            supportingContent = {
                Column {
                    if (word.context.isNotEmpty())
                        Text(word.context, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    if (word.profiles.isEmpty()) Text("All profiles")
                    else
                        Text(
                            profiles.filterKeys { it in word.profiles }.values.joinToString(", "),
                            color = MaterialTheme.colorScheme.onSurface,
                        )
                }
            },
            // The desktop's Key term column: a microphone when the speech service gets the word,
            // faint for a key term it does not. ListItem tops the leading and trailing slots of a
            // three-line row, so they fill its height.
            leadingContent = {
                Box(Modifier.fillMaxHeight(), contentAlignment = Alignment.Center) {
                    // Without case, as the desktop matches: Claude keeps one
                    // spelling of a word an earlier release let in twice.
                    val sent = keyTerms.any { it.equals(word.term, ignoreCase = true) }
                    IconSlot(
                        R.drawable.ic_mic,
                        when {
                            !word.keyTerm -> null
                            sent -> "Key term: sent to the speech service as a hint."
                            settings.transcriptionProvider == Provider.Claude ->
                                "Key term, but the speech service does not take it, so it is not sent."
                            else -> "Key term, but this speech service takes none."
                        },
                        faint = !sent,
                    )
                }
            },
            trailingContent = {
                Box(Modifier.fillMaxHeight(), contentAlignment = Alignment.Center) {
                    IconButton({
                        onChange(settings.copy(vocabulary = settings.vocabulary - word))
                    }) {
                        Icon(
                            painterResource(R.drawable.ic_close),
                            contentDescription = "Remove ${word.term}",
                        )
                    }
                }
            },
            modifier = Modifier.height(IntrinsicSize.Min).clickable { editing = word },
            colors = rowColors(),
        )
    }
    FilledTonalButton({ editing = VocabularyWord("") }, Modifier.padding(16.dp)) {
        Text("Add word")
    }
    editing?.let { word ->
        WordEditor(
            word,
            profiles,
            settings.vocabulary.filter { it != word }.map { it.term },
            onDismiss = { editing = null },
        ) { next ->
            val vocabulary =
                if (word in settings.vocabulary)
                    settings.vocabulary.map { if (it == word) next else it }
                else settings.vocabulary + next
            onChange(settings.copy(vocabulary = vocabulary))
            editing = null
        }
    }
}

/**
 * One of a vocabulary row's leading icon slots, a fixed width so the rows line up. Empty without a
 * [description]; with one, the icon in the text colour, or the disabled colour when [faint], and
 * the description as its tooltip.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun IconSlot(icon: Int, description: String?, faint: Boolean = false) {
    Box(Modifier.size(24.dp)) {
        if (description == null) return@Box
        TooltipBox(
            TooltipDefaults.rememberTooltipPositionProvider(TooltipAnchorPosition.Above),
            tooltip = { PlainTooltip { Text(description) } },
            state = rememberTooltipState(),
        ) {
            val colors = rowColors()
            Icon(
                painterResource(icon),
                contentDescription = description,
                tint = if (faint) colors.disabledLeadingIconColor else colors.headlineColor,
            )
        }
    }
}

/**
 * A word's term, its context and the profiles it is limited to, in a sheet. Saved once the term is
 * set and, unless it is the word's own term, none of [taken], ignoring case, and a limit names at
 * least one profile. Its own term passes so a list that earlier releases let hold two spellings of
 * a word can still be edited.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun WordEditor(
    word: VocabularyWord,
    profiles: Map<WritingProfile, String>,
    taken: List<String>,
    onDismiss: () -> Unit,
    onSave: (VocabularyWord) -> Unit,
) {
    var term by rememberSaveable { mutableStateOf(word.term) }
    var context by rememberSaveable { mutableStateOf(word.context) }
    var keyTerm by rememberSaveable { mutableStateOf(word.keyTerm) }
    var limited by rememberSaveable { mutableStateOf(word.profiles.isNotEmpty()) }
    var chosen by remember { mutableStateOf(word.profiles) }
    val duplicate =
        term.trim() != word.term && taken.any { it.equals(term.trim(), ignoreCase = true) }
    ModalBottomSheet(onDismiss, sheetState = rememberModalBottomSheetState(true)) {
        Column(
            Modifier.verticalScroll(rememberScrollState()).padding(bottom = 16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            Text(
                word.term.ifEmpty { "Add word" },
                Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.headlineSmall,
            )
            OutlinedTextField(
                term,
                { term = it },
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                label = { Text("Term") },
                singleLine = true,
                isError = duplicate,
                supportingText =
                    if (duplicate) ({ Text("That word is already listed.") }) else null,
            )
            ChoiceRow(
                "Key term",
                Role.Checkbox,
                keyTerm,
                supporting =
                    "Sent to the speech service as a hint, so it hears the term. Refinement uses " +
                        "every term either way.",
            ) {
                keyTerm = !keyTerm
            }
            OutlinedTextField(
                context,
                { context = it },
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                label = { Text("Context") },
                supportingText = {
                    Text("Refinement reads this to decide when the words you said mean this term.")
                },
                minLines = 2,
                maxLines = 6,
            )
            Column(Modifier.selectableGroup()) {
                Text(
                    "Profiles",
                    Modifier.padding(horizontal = 16.dp),
                    style = MaterialTheme.typography.titleSmall,
                )
                ChoiceRow("Every Writing Profile", Role.RadioButton, !limited) { limited = false }
                ChoiceRow("Only these Writing Profiles:", Role.RadioButton, limited) {
                    limited = true
                }
            }
            Column(Modifier.padding(start = 40.dp)) {
                profiles.forEach { (profile, label) ->
                    ChoiceRow(label, Role.Checkbox, profile in chosen, enabled = limited) {
                        chosen = if (profile in chosen) chosen - profile else chosen + profile
                    }
                }
            }
            Text(
                "Under any other profile, neither refinement nor Claude Voice gets this term.",
                Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            SheetActions(
                term.isNotBlank() && !duplicate && (!limited || chosen.isNotEmpty()),
                onDismiss,
                "Save",
            ) {
                onSave(
                    VocabularyWord(
                        term.trim(),
                        context.trim(),
                        if (limited) chosen else emptySet(),
                        keyTerm,
                        // Kept, though no longer edited: a word given priority before stays first.
                        word.priority,
                    )
                )
            }
        }
    }
}

/**
 * A radio button or checkbox and its label, with [supporting] text under it if given, the whole row
 * the target.
 */
@Composable
private fun ChoiceRow(
    label: String,
    role: Role,
    selected: Boolean,
    enabled: Boolean = true,
    supporting: String? = null,
    onClick: () -> Unit,
) {
    val target =
        if (role == Role.RadioButton)
            Modifier.selectable(selected, enabled = enabled, role = role, onClick = onClick)
        else Modifier.toggleable(selected, enabled = enabled, role = role) { onClick() }
    Row(
        Modifier.fillMaxWidth().then(target).padding(horizontal = 16.dp, vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        if (role == Role.RadioButton) RadioButton(selected, null, enabled = enabled)
        else Checkbox(selected, null, enabled = enabled)
        // Material's disabled content opacity, as the control beside it uses.
        val alpha = if (enabled) 1f else 0.38f
        Column(Modifier.padding(start = 16.dp)) {
            Text(
                label,
                style = MaterialTheme.typography.bodyLarge,
                color = MaterialTheme.colorScheme.onSurface.copy(alpha = alpha),
            )
            supporting?.let {
                Text(
                    it,
                    style = MaterialTheme.typography.bodyMedium,
                    color = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = alpha),
                )
            }
        }
    }
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
 * A row named [title] whose value opens a menu of [options]; the whole row is the target. The value
 * shows as [selectedLabel], which names a [selected] value the menu doesn't offer.
 */
@Composable
internal fun <T> DropdownRow(
    title: String,
    options: Map<T, String>,
    selected: T,
    description: String? = null,
    selectedLabel: String = options[selected] ?: selected.toString(),
    onSelect: (T) -> Unit,
) {
    var expanded by remember { mutableStateOf(false) }
    ListItem(
        headlineContent = { Text(title) },
        supportingContent = description?.let { { Text(it) } },
        trailingContent = {
            Box {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        selectedLabel,
                        style = MaterialTheme.typography.bodyLarge,
                        color = MaterialTheme.colorScheme.primary,
                    )
                    Icon(painterResource(R.drawable.ic_arrow_drop_down), contentDescription = null)
                }
                DropdownMenu(expanded, { expanded = false }) {
                    options.forEach { (value, label) ->
                        DropdownMenuItem(
                            text = { Text(label) },
                            onClick = {
                                expanded = false
                                onSelect(value)
                            },
                        )
                    }
                }
            }
        },
        modifier = Modifier.clickable(role = Role.DropdownList) { expanded = true },
        colors = rowColors(),
    )
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
private fun spokenLanguageChoices(provider: Provider): Map<String, String> =
    provider.spokenLanguages
        .sortedWith(compareBy({ it != AUTOMATIC_LANGUAGE }, ::spokenLanguageName))
        .associateWith(::spokenLanguageLabel)

/**
 * The words Claude Voice receives as key terms: those marked so, priority first, as many as fit its
 * header.
 */
internal fun keyTerms(settings: SpeecherSettings): Set<String> {
    if (settings.transcriptionProvider != Provider.Claude) return emptySet()
    val terms = speechTerms(settings.vocabulary)
    return claudeVoiceKeytermIndices(terms).map { terms[it] }.toSet()
}

/**
 * What the list amounts to: refinement uses every term up to its ceiling, and only Claude takes key
 * terms, as many as fit its header.
 */
internal fun vocabularySummary(settings: SpeecherSettings): String {
    val count = settings.vocabulary.size
    val refinement =
        if (count > MAX_REFINEMENT_TERMS) "the first $MAX_REFINEMENT_TERMS are used for refinement"
        else "refinement uses every word for the dictation's Writing Profile"
    if (settings.transcriptionProvider != Provider.Claude) {
        return "Names and terms Speecher should spell your way. ChatGPT dictation takes no " +
            "key terms, and $refinement."
    }
    val hints = claudeVoiceKeyterms(speechTerms(settings.vocabulary)).size
    val keyTerms = if (hints == 1) "1 key term" else "$hints key terms"
    return "Names and terms Speecher should spell your way. Claude takes $keyTerms, and " +
        "$refinement."
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
            )
        }
    }
}

@PreviewLightDark
@Composable
internal fun SettingsRefinementHaikuPreview() = SpeecherTheme {
    var settings by remember {
        mutableStateOf(
            SpeecherSettings(
                refinementProvider = Provider.Claude,
                claudeRefinement = RefinementChoice("claude-haiku-4-5", "xhigh"),
            )
        )
    }
    Surface {
        Column {
            SettingsPageContent(
                SettingsPage.Refinement,
                settings,
                setOf(Provider.Claude),
                { settings = it },
                {},
                {},
            )
        }
    }
}

@PreviewLightDark
@Composable
internal fun SettingsVocabularyPreview() = SpeecherTheme {
    val standup = WritingProfile("custom_standup")
    var settings by remember {
        mutableStateOf(
            SpeecherSettings(
                transcriptionProvider = Provider.Claude,
                writingProfiles =
                    SpeecherSettings().writingProfiles +
                        (standup to WritingProfileSettings(name = "Standup notes")),
                vocabulary =
                    listOf(
                        VocabularyWord(
                            "Kubernetes",
                            "The container platform, when I talk about clusters, pods or deploys.",
                            setOf(WritingProfile.Work, WritingProfile.AiCoding),
                            priority = true,
                        ),
                        VocabularyWord("Speecher", priority = true),
                        VocabularyWord("Lúcia", "My sister.", setOf(WritingProfile.Personal)),
                        VocabularyWord(
                            "Grafana",
                            "",
                            setOf(WritingProfile.Work, standup),
                            keyTerm = false,
                        ),
                    ),
            )
        )
    }
    SpeecherScreen(SettingsPage.Vocabulary.title, onBack = {}) {
        SettingsPageContent(
            SettingsPage.Vocabulary,
            settings,
            Provider.entries.toSet(),
            { settings = it },
            {},
            {},
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
        )
    }
}
