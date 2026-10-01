package app.speecher.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Badge
import androidx.compose.material3.Card
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
import androidx.compose.material3.ListItemColors
import androidx.compose.material3.ListItemDefaults
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
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.OpenAiSpeed
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.hasBatchTranscription
import app.speecher.android.dictation.label
import app.speecher.android.dictation.providerOrder
import app.speecher.android.dictation.refinementEfforts
import app.speecher.android.dictation.refinementModels
import app.speecher.android.dictation.resolveSignedIn
import app.speecher.protocol.MAX_REFINEMENT_TERMS
import app.speecher.protocol.claudeVoiceKeytermIndices
import app.speecher.protocol.claudeVoiceKeyterms
import app.speecher.protocol.modelSupportsFastMode
import app.speecher.protocol.modelSupportsUltrafast

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
    modifier: Modifier = Modifier,
    sessionEnded: Set<Provider> = emptySet(),
    signingIn: Provider? = null,
    signInError: String? = null,
    onPasteCode: (String) -> Unit = {},
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
                    when {
                        ended -> TextButton({ onSignIn(provider) }) { Text("Sign in again") }
                        isSignedIn -> TextButton({ onSignOut(provider) }) { Text("Sign out") }
                        else -> TextButton({ onSignIn(provider) }) { Text("Sign in") }
                    }
                },
                colors = rowColors(),
            )
        }
        if (signingIn != null && signingIn !in signedIn) {
            PasteCode(signingIn, onPasteCode, Modifier.padding(horizontal = 16.dp))
        }

        Section("Dictation")
        PageRow(SettingsPage.Transcription, transcriptionSummary(settings, signedIn), onOpen)
        PageRow(SettingsPage.Refinement, refinementSummary(settings, signedIn), onOpen)
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

private fun transcriptionSummary(settings: SpeecherSettings, signedIn: Set<Provider>): String =
    shownProvider(settings.transcriptionProvider, signedIn)?.label ?: "Not signed in"

private fun refinementSummary(settings: SpeecherSettings, signedIn: Set<Provider>): String {
    if (!settings.refinementEnabled) return "Off"
    val provider = shownProvider(settings.refinementProvider, signedIn) ?: return "Not signed in"
    val model = settings.refinement(provider).model
    return "${provider.label}, ${provider.refinementModels[model] ?: model}"
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
    ListItem(
        headlineContent = { Text("Keep screen on") },
        supportingContent = { Text("Stops the screen turning off while you dictate.") },
        trailingContent = {
            Switch(settings.keepScreenOn, { onChange(settings.copy(keepScreenOn = it)) })
        },
        colors = rowColors(),
    )
    if (resolveSignedIn(settings.transcriptionProvider, signedIn).hasBatchTranscription) {
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
    DropdownRow("Model", provider.refinementModels, choice.model) {
        onChange(settings.withRefinement(provider, choice.copy(model = it)))
    }
    DropdownRow(
        "Reasoning effort",
        provider.refinementEfforts.associateWith { it.replaceFirstChar(Char::uppercase) },
        choice.effort,
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
    settings.vocabulary.forEach { word ->
        ListItem(
            headlineContent = {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(word)
                    if (word in keyTerms) {
                        Badge(
                            Modifier.padding(start = 8.dp),
                            containerColor = MaterialTheme.colorScheme.primaryContainer,
                            contentColor = MaterialTheme.colorScheme.onPrimaryContainer,
                        ) {
                            Text("Key term")
                        }
                    }
                }
            },
            trailingContent = {
                IconButton({ onChange(settings.copy(vocabulary = settings.vocabulary - word)) }) {
                    Icon(painterResource(R.drawable.ic_close), contentDescription = "Remove $word")
                }
            },
            colors = rowColors(),
        )
    }
    AddWord { word ->
        if (word !in settings.vocabulary) {
            onChange(settings.copy(vocabulary = settings.vocabulary + word))
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

/** A row named [title] whose value opens a menu of [options]; the whole row is the target. */
@Composable
internal fun <T> DropdownRow(
    title: String,
    options: Map<T, String>,
    selected: T,
    description: String? = null,
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
                        options[selected] ?: selected.toString(),
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

/** The words Claude Voice receives as key terms: as many as fit its header, in list order. */
internal fun keyTerms(settings: SpeecherSettings): Set<String> =
    if (settings.transcriptionProvider != Provider.Claude) emptySet()
    else claudeVoiceKeytermIndices(settings.vocabulary).map { settings.vocabulary[it] }.toSet()

/**
 * What the list amounts to: refinement uses every term up to its ceiling, and only Claude takes key
 * terms, as many as fit its header.
 */
internal fun vocabularySummary(settings: SpeecherSettings): String {
    val count = settings.vocabulary.size
    val refinement =
        if (count > MAX_REFINEMENT_TERMS) "the first $MAX_REFINEMENT_TERMS are used for refinement"
        else "all are used for refinement"
    if (settings.transcriptionProvider != Provider.Claude) {
        return "Names and terms Speecher should spell your way. ChatGPT dictation takes no " +
            "key terms, and $refinement."
    }
    val hints = claudeVoiceKeyterms(settings.vocabulary).size
    return "Names and terms Speecher should spell your way. Claude takes the $hints marked Key " +
        "term, and $refinement."
}

@Composable
private fun AddWord(onAdd: (String) -> Unit) {
    var word by rememberSaveable { mutableStateOf("") }
    val submit = {
        if (word.isNotBlank()) onAdd(word.trim())
        word = ""
    }
    Row(
        Modifier.padding(start = 16.dp, end = 4.dp, top = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        OutlinedTextField(
            word,
            { word = it },
            Modifier.weight(1f),
            placeholder = { Text("Add a word") },
            singleLine = true,
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Done),
            keyboardActions = KeyboardActions(onDone = { submit() }),
        )
        TextButton(submit, enabled = word.isNotBlank()) { Text("Add") }
    }
}

@Composable
private fun SettingsPreview(settings: SpeecherSettings, signedIn: Set<Provider>) = SpeecherTheme {
    Surface { Settings(settings, signedIn, {}, {}, {}) }
}

@PreviewLightDark
@Composable
internal fun SettingsPreview() =
    SettingsPreview(
        SpeecherSettings(vocabulary = listOf("Speecher", "Kirigami", "Priya Raman")),
        Provider.entries.toSet(),
    )

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
