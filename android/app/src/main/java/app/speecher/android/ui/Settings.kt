package app.speecher.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.Badge
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.ListItem
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
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SpeecherSettings
import app.speecher.android.dictation.label
import app.speecher.android.dictation.profileDeletionNotice
import app.speecher.android.dictation.providerOrder
import app.speecher.android.dictation.refinementEfforts
import app.speecher.android.dictation.refinementModels
import app.speecher.protocol.AppCategory
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.CustomCleanupLevel
import app.speecher.protocol.CustomTone
import app.speecher.protocol.MAX_REFINEMENT_TERMS
import app.speecher.protocol.RecognitionRule
import app.speecher.protocol.Tone
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings
import app.speecher.protocol.builtInDictationSystemPrompt
import app.speecher.protocol.builtInRules
import app.speecher.protocol.claudeVoiceKeytermIndices
import app.speecher.protocol.claudeVoiceKeyterms
import app.speecher.protocol.cleanupLevelId
import app.speecher.protocol.customChoiceId
import app.speecher.protocol.modelSupportsFastMode
import app.speecher.protocol.toneId
import app.speecher.protocol.withCleanupLevel
import app.speecher.protocol.withTone

internal const val FAST_MODE_DESCRIPTION =
    "Makes refinement faster. Uses a little more of your usage, but the difference is tiny."
private const val FAST_MODE_OPUS_ONLY = "Only works with Opus models."

// Labels from the desktop's Target.cpp, in its order.
private val profileLabels =
    mapOf(
        WritingProfile.Work to "Work",
        WritingProfile.Email to "Email",
        WritingProfile.Personal to "Personal",
        WritingProfile.AiCoding to "AI coding",
        WritingProfile.Other to "Other",
    )

// The desktop's appCategoryLabel, in the order its rules offer them.
private val appTypeLabels =
    mapOf(
        AppCategory.General to "Other app",
        AppCategory.Terminal to "Terminal",
        AppCategory.Browser to "Browser",
        AppCategory.Email to "Email",
        AppCategory.Office to "Office",
        AppCategory.CodeEditor to "Code editor",
        AppCategory.AiCoding to "AI coding",
    )

private val cleanupLabels =
    mapOf(
        CleanupStrength.None to "None",
        CleanupStrength.LightCleanup to "Light",
        CleanupStrength.Balanced to "Medium",
        CleanupStrength.StrongPolish to "High",
    )

/** What a custom cleanup level can build on. */
private val baseLabels =
    cleanupLabels - CleanupStrength.None + (CleanupStrength.CustomOnly to "Custom only")

private val toneLabels =
    mapOf(
        Tone.None to "No tone override",
        Tone.Formal to "Formal",
        Tone.Casual to "Casual",
        Tone.VeryCasual to "Very casual",
        Tone.Excited to "Excited",
        Tone.GenZ to "Gen Z",
    )

/** Every level a profile can choose, by id: the built-ins, then the custom ones. */
internal fun cleanupChoices(settings: SpeecherSettings): Map<String, String> =
    cleanupLabels.mapKeys { it.key.id } +
        settings.customCleanupLevels.associate { it.id to it.name }

/** Every profile, by id: the built-ins, then the custom ones by name. */
internal fun profileChoices(settings: SpeecherSettings): Map<WritingProfile, String> =
    profileLabels +
        settings.writingProfiles.filterKeys { !it.isBuiltIn }.mapValues { it.value.name }

/** Every tone a profile can choose, by id: no override and the built-ins, then the custom ones. */
internal fun toneChoices(settings: SpeecherSettings): Map<String, String> =
    toneLabels.mapKeys { it.key.id } + settings.customTones.associate { it.id to it.name }

/** Settings. Every change goes out whole through [onChange]; the caller persists it. */
@Composable
fun Settings(
    settings: SpeecherSettings,
    signedIn: Set<Provider>,
    onChange: (SpeecherSettings) -> Unit,
    onSignIn: (Provider) -> Unit,
    onSignOut: (Provider) -> Unit,
    onSetChipPosition: () -> Unit,
    modifier: Modifier = Modifier,
    signingIn: Provider? = null,
    signInError: String? = null,
    onPasteCode: (String) -> Unit = {},
) {
    val rowColors = ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.surface)
    Column(modifier) {
        Section("Transcription")
        ProviderPicker("Transcription provider", settings.transcriptionProvider, signedIn) {
            onChange(settings.copy(transcriptionProvider = it))
        }
        ListItem(
            headlineContent = { Text("Keep screen on") },
            supportingContent = { Text("Stops the screen turning off while you dictate.") },
            trailingContent = {
                Switch(settings.keepScreenOn, { onChange(settings.copy(keepScreenOn = it)) })
            },
            colors = rowColors,
        )

        Section("Refinement")
        ListItem(
            headlineContent = { Text("Offer Insert refined") },
            supportingContent = { Text("Clean up filler words and punctuation before inserting.") },
            trailingContent = {
                Switch(
                    settings.refinementEnabled,
                    { onChange(settings.copy(refinementEnabled = it)) },
                )
            },
            colors = rowColors,
        )
        if (settings.refinementEnabled) {
            ProviderPicker("Refinement provider", settings.refinementProvider, signedIn) {
                onChange(settings.copy(refinementProvider = it))
            }
            val provider = settings.refinementProvider
            val choice = settings.refinement(provider)
            ListItem(
                headlineContent = { Text("Model") },
                trailingContent = {
                    Dropdown(provider.refinementModels, choice.model) {
                        onChange(settings.withRefinement(provider, choice.copy(model = it)))
                    }
                },
                colors = rowColors,
            )
            EffortPicker(provider, choice.effort) {
                onChange(settings.withRefinement(provider, choice.copy(effort = it)))
            }
            val unsupported = provider == Provider.Claude && !modelSupportsFastMode(choice.model)
            ListItem(
                headlineContent = { Text("Fast mode") },
                supportingContent = {
                    Text(if (unsupported) FAST_MODE_OPUS_ONLY else FAST_MODE_DESCRIPTION)
                },
                trailingContent = {
                    Switch(
                        settings.fastMode(provider),
                        { onChange(settings.withFastMode(provider, it)) },
                    )
                },
                colors = rowColors,
            )
            ListItem(
                headlineContent = { Text("Fallback profile") },
                supportingContent = {
                    Text("Writing profile used when the target app does not imply one.")
                },
                trailingContent = {
                    Dropdown(profileChoices(settings), settings.defaultWritingProfile) {
                        onChange(settings.copy(defaultWritingProfile = it))
                    }
                },
                colors = rowColors,
            )
            ListItem(
                headlineContent = { Text("Context") },
                supportingContent = {
                    Text(
                        "Sends the field's placeholder and the text around the cursor to your " +
                            "refinement provider. The app's name is always sent, to pick a " +
                            "writing profile."
                    )
                },
                trailingContent = {
                    Switch(
                        settings.useTargetContext,
                        { onChange(settings.copy(useTargetContext = it)) },
                    )
                },
                colors = rowColors,
            )
            if (settings.useTargetContext) {
                ListItem(
                    headlineContent = { Text("Screen text") },
                    supportingContent = {
                        Text(
                            "Reads the visible text of the app you're dictating into and sends " +
                                "it to your refinement provider. Speecher discards it when the " +
                                "dictation ends."
                        )
                    },
                    trailingContent = {
                        Switch(
                            settings.includeScreenText,
                            { onChange(settings.copy(includeScreenText = it)) },
                        )
                    },
                    colors = rowColors,
                )
                ListItem(
                    headlineContent = { Text("Screenshot") },
                    supportingContent = {
                        Text(
                            "Sends a screenshot of the app you're dictating into, without the " +
                                "status bar or keyboard, to your refinement provider. Needs a " +
                                "vision-capable refinement model, such as Claude Sonnet 5."
                        )
                    },
                    trailingContent = {
                        Switch(
                            settings.includeScreenshot,
                            { onChange(settings.copy(includeScreenshot = it)) },
                        )
                    },
                    colors = rowColors,
                )
            }
            InstructionsField(
                "Additional instructions",
                "Added to every refinement, before each profile's own instructions.",
                settings.additionalInstructions,
            ) {
                onChange(settings.copy(additionalInstructions = it))
            }
        }
        ListItem(
            headlineContent = { Text("Extra transcription pass") },
            supportingContent = {
                Text(
                    "Re-transcribes your audio with GPT Transcribe before Insert and Insert " +
                        "refined, for accuracy — slower. ChatGPT only."
                )
            },
            trailingContent = {
                Switch(
                    settings.transcribePassEnabled,
                    { onChange(settings.copy(transcribePassEnabled = it)) },
                )
            },
            colors = rowColors,
        )
        if (settings.refinementEnabled) {
            Section("Profile behavior")
            Text(
                "Choose a cleanup level, a tone and optional instructions for each profile.",
                Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            val profiles = profileChoices(settings)
            profiles.forEach { (profile, label) ->
                val behavior = settings.writingProfiles.getValue(profile)
                fun update(next: WritingProfileSettings) =
                    onChange(
                        settings.copy(
                            writingProfiles = settings.writingProfiles + (profile to next)
                        )
                    )
                ListItem(
                    headlineContent = {
                        if (profile.isBuiltIn) Text(label)
                        else
                            OutlinedTextField(
                                behavior.name,
                                { update(behavior.copy(name = it)) },
                                Modifier.fillMaxWidth(),
                                label = { Text("Name") },
                                singleLine = true,
                                isError = behavior.name.isBlank(),
                            )
                    },
                    supportingContent = {
                        Column {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Dropdown(cleanupChoices(settings), behavior.cleanupLevelId) {
                                    update(behavior.withCleanupLevel(it))
                                }
                                Dropdown(toneChoices(settings), behavior.toneId) {
                                    update(behavior.withTone(it))
                                }
                            }
                            OutlinedTextField(
                                behavior.instructions,
                                { update(behavior.copy(instructions = it)) },
                                Modifier.fillMaxWidth(),
                                label = { Text("Instructions") },
                                minLines = 2,
                            )
                            if (!profile.isBuiltIn) {
                                DeleteProfile(settings.profileDeletionNotice(profile)) {
                                    onChange(
                                        settings.withWritingProfiles(
                                            settings.writingProfiles - profile
                                        )
                                    )
                                }
                            }
                        }
                    },
                    colors = rowColors,
                )
            }
            AddChoice("Add profile", "Instructions", profiles.values) { name, instructions ->
                val id = customChoiceId(name, settings.writingProfiles.keys.map { it.id })
                onChange(
                    settings.copy(
                        writingProfiles =
                            settings.writingProfiles +
                                (WritingProfile(id) to
                                    WritingProfileSettings(
                                        instructions = instructions,
                                        name = name,
                                    ))
                    )
                )
            }

            Section("Application rules")
            Text(
                "Built-in rules are read-only. Your rules come first and can set the app type, " +
                    "the writing profile, or both.",
                Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            BuiltInRules(profiles)
            val appTypes = mapOf<AppCategory?, String>(null to "Automatic") + appTypeLabels
            val ruleProfiles = mapOf<WritingProfile?, String>(null to "Automatic") + profiles
            settings.appRules.forEachIndexed { index, rule ->
                fun edit(next: RecognitionRule) =
                    onChange(
                        settings.copy(
                            appRules =
                                settings.appRules.mapIndexed { at, it ->
                                    if (at == index) next else it
                                }
                        )
                    )
                RuleEditor(
                    rule,
                    appTypes,
                    ruleProfiles,
                    onEdit = ::edit,
                    onDelete = {
                        onChange(
                            settings.copy(
                                appRules = settings.appRules.filterIndexed { at, _ -> at != index }
                            )
                        )
                    },
                )
            }
            AddRule(appTypes, ruleProfiles) {
                onChange(settings.copy(appRules = settings.appRules + it))
            }

            Section("Tones")
            BuiltInNames(toneLabels.values.drop(1))
            settings.customTones.forEach { tone ->
                CustomChoice(
                    tone.name,
                    tone.instruction,
                    "Instruction",
                    onEdit = { name, text ->
                        onChange(
                            settings.withCustomChoices(
                                tones =
                                    settings.customTones.map {
                                        if (it.id == tone.id)
                                            it.copy(name = name, instruction = text)
                                        else it
                                    }
                            )
                        )
                    },
                    onDelete = {
                        onChange(settings.withCustomChoices(tones = settings.customTones - tone))
                    },
                )
            }
            AddChoice("Add tone", "Instruction", toneChoices(settings).values, needsText = true) {
                name,
                instruction ->
                val id = customChoiceId(name, settings.customTones.map { it.id })
                onChange(
                    settings.withCustomChoices(
                        tones = settings.customTones + CustomTone(id, name, instruction)
                    )
                )
            }

            Section("Cleanup levels")
            BuiltInNames(cleanupLabels.values.drop(1))
            settings.customCleanupLevels.forEach { level ->
                fun edit(next: CustomCleanupLevel) =
                    onChange(
                        settings.withCustomChoices(
                            levels =
                                settings.customCleanupLevels.map {
                                    if (it.id == level.id) next else it
                                }
                        )
                    )
                CustomChoice(
                    level.name,
                    level.instructions,
                    "Instructions",
                    onEdit = { name, text -> edit(level.copy(name = name, instructions = text)) },
                    onDelete = {
                        onChange(
                            settings.withCustomChoices(
                                levels = settings.customCleanupLevels - level
                            )
                        )
                    },
                ) {
                    Dropdown(baseLabels, level.base) { edit(level.copy(base = it)) }
                }
            }
            // A new level builds on Medium; its base can be changed once it is added.
            AddChoice("Add cleanup level", "Instructions", cleanupChoices(settings).values) {
                name,
                instructions ->
                val id = customChoiceId(name, settings.customCleanupLevels.map { it.id })
                onChange(
                    settings.withCustomChoices(
                        levels =
                            settings.customCleanupLevels +
                                CustomCleanupLevel(id, name, CleanupStrength.Balanced, instructions)
                    )
                )
            }

            Section("Custom system prompt")
            ListItem(
                headlineContent = { Text("Custom system prompt") },
                supportingContent = {
                    Text(
                        "Replaces the built-in dictation rules with the prompt below. Built-in " +
                            "cleanup levels and tones no longer apply while it is on; a " +
                            "profile's tone is still passed to the model."
                    )
                },
                trailingContent = {
                    Switch(
                        settings.customSystemPromptEnabled,
                        { onChange(settings.copy(customSystemPromptEnabled = it)) },
                    )
                },
                colors = rowColors,
            )
            OutlinedTextField(
                settings.customSystemPrompt.ifEmpty { builtInDictationSystemPrompt },
                { onChange(settings.copy(customSystemPrompt = it)) },
                Modifier.fillMaxWidth().padding(horizontal = 16.dp),
                label = { Text("Prompt") },
                minLines = 4,
                maxLines = 10,
            )
            TextButton(
                { onChange(settings.copy(customSystemPrompt = builtInDictationSystemPrompt)) },
                Modifier.padding(horizontal = 8.dp),
            ) {
                Text("Reset to built-in")
            }
        }

        // With refinement off the panel only offers Insert, so there is no layout to choose.
        if (settings.refinementEnabled) {
            Section("Buttons")
            ButtonLayoutPicker(settings.buttonLayout) { onChange(settings.copy(buttonLayout = it)) }
        }
        Section("Dictation panel size")
        PanelSizePicker(settings.panelSize) { onChange(settings.copy(panelSize = it)) }

        Section("Dictation button")
        ListItem(
            headlineContent = { Text("Place on the keyboard's mic key") },
            supportingContent = {
                Text(
                    "Turn off to put the button where you choose. Dragging it moves it until the keyboard closes."
                )
            },
            trailingContent = {
                Switch(settings.chipDockOnMic, { onChange(settings.copy(chipDockOnMic = it)) })
            },
            colors = rowColors,
        )
        if (!settings.chipDockOnMic) {
            ListItem(
                headlineContent = { Text("Set button position") },
                trailingContent = {
                    Icon(painterResource(R.drawable.ic_chevron_right), contentDescription = null)
                },
                modifier = Modifier.clickable(onClick = onSetChipPosition),
                colors = rowColors,
            )
        }

        Section("Vocabulary")
        Text(
            vocabularySummary(settings),
            Modifier.padding(horizontal = 16.dp),
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
                    IconButton({
                        onChange(settings.copy(vocabulary = settings.vocabulary - word))
                    }) {
                        Icon(
                            painterResource(R.drawable.ic_close),
                            contentDescription = "Remove $word",
                        )
                    }
                },
                colors = rowColors,
            )
        }
        AddWord { word ->
            if (word !in settings.vocabulary) {
                onChange(settings.copy(vocabulary = settings.vocabulary + word))
            }
        }

        Section("Accounts")
        signInError?.let {
            Text(it, Modifier.padding(horizontal = 16.dp), color = MaterialTheme.colorScheme.error)
        }
        providerOrder.forEach { provider ->
            val isSignedIn = provider in signedIn
            ListItem(
                headlineContent = { Text(provider.label) },
                supportingContent = { Text(if (isSignedIn) "Signed in" else "Signed out") },
                trailingContent = {
                    if (isSignedIn) {
                        TextButton({ onSignOut(provider) }) { Text("Sign out") }
                    } else {
                        TextButton({ onSignIn(provider) }) { Text("Sign in") }
                    }
                },
                colors = rowColors,
            )
        }
        if (signingIn != null) PasteCode(signingIn, onPasteCode)
    }
}

@Composable
private fun ProviderPicker(
    label: String,
    selected: Provider,
    signedIn: Set<Provider>,
    onSelect: (Provider) -> Unit,
) {
    SingleChoiceSegmentedButtonRow(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp).semantics {
            contentDescription = label
        }
    ) {
        providerOrder.forEachIndexed { index, provider ->
            // A provider you aren't signed into can't be used — dictation would silently fall back
            // to the other account — so it's disabled here until you connect it in Accounts below.
            SegmentedButton(
                selected = provider == selected,
                onClick = { onSelect(provider) },
                enabled = provider in signedIn,
                shape = SegmentedButtonDefaults.itemShape(index, providerOrder.size),
            ) {
                Text(provider.label)
            }
        }
    }
}

/** A text button showing [options]' label for [selected] that opens a menu of all of them. */
@Composable
private fun <T> Dropdown(options: Map<T, String>, selected: T, onSelect: (T) -> Unit) {
    var expanded by remember { mutableStateOf(false) }
    Box {
        TextButton({ expanded = true }) { Text(options[selected] ?: selected.toString()) }
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
}

@Composable
private fun EffortPicker(provider: Provider, selected: String, onSelect: (String) -> Unit) {
    val efforts = provider.refinementEfforts
    SingleChoiceSegmentedButtonRow(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp).semantics {
            contentDescription = "Reasoning effort"
        }
    ) {
        efforts.forEachIndexed { index, effort ->
            SegmentedButton(
                selected = effort == selected,
                onClick = { onSelect(effort) },
                shape = SegmentedButtonDefaults.itemShape(index, efforts.size),
            ) {
                Text(effort.replaceFirstChar(Char::uppercase))
            }
        }
    }
}

/** The built-in tones or levels, which cannot be edited, as one line. */
@Composable
private fun BuiltInNames(names: List<String>) {
    Text(
        "Built-in: ${names.joinToString(", ")}",
        Modifier.padding(horizontal = 16.dp),
        style = MaterialTheme.typography.bodyMedium,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

/**
 * One custom tone or level: its name and its instructions, editable, with [extra] controls beside
 * the name and a delete button.
 */
@Composable
private fun CustomChoice(
    name: String,
    text: String,
    textLabel: String,
    onEdit: (name: String, text: String) -> Unit,
    onDelete: () -> Unit,
    extra: @Composable () -> Unit = {},
) {
    Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                name,
                { onEdit(it, text) },
                Modifier.weight(1f),
                label = { Text("Name") },
                singleLine = true,
                isError = name.isBlank(),
            )
            extra()
            IconButton(onDelete) {
                Icon(painterResource(R.drawable.ic_close), contentDescription = "Delete $name")
            }
        }
        OutlinedTextField(
            text,
            { onEdit(name, it) },
            Modifier.fillMaxWidth(),
            label = { Text(textLabel) },
            minLines = 2,
        )
    }
}

/**
 * A name and instructions for a new tone or level, added once the name is set and unlike [taken],
 * and the instructions are set when [needsText]. The id comes from the name given here.
 */
@Composable
private fun AddChoice(
    action: String,
    textLabel: String,
    taken: Collection<String>,
    needsText: Boolean = false,
    onAdd: (name: String, text: String) -> Unit,
) {
    var name by rememberSaveable { mutableStateOf("") }
    var text by rememberSaveable { mutableStateOf("") }
    val duplicate = taken.any { it.equals(name.trim(), ignoreCase = true) }
    Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        OutlinedTextField(
            name,
            { name = it },
            Modifier.fillMaxWidth(),
            label = { Text("Name") },
            singleLine = true,
            isError = duplicate,
            supportingText = if (duplicate) ({ Text("That name is taken.") }) else null,
        )
        OutlinedTextField(
            text,
            { text = it },
            Modifier.fillMaxWidth(),
            label = { Text(textLabel) },
            minLines = 2,
        )
        TextButton(
            {
                onAdd(name.trim(), text)
                name = ""
                text = ""
            },
            enabled = name.isNotBlank() && !duplicate && (!needsText || text.isNotBlank()),
        ) {
            Text(action)
        }
    }
}

/**
 * Deletes a custom profile. When a rule or the fallback points at it, the first tap shows what the
 * delete changes and asks again, as the desktop's confirmation does.
 */
@Composable
private fun DeleteProfile(notice: String, onDelete: () -> Unit) {
    var confirming by remember { mutableStateOf(false) }
    if (confirming) {
        Text(
            notice,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
    Row {
        if (confirming) TextButton({ confirming = false }) { Text("Cancel") }
        TextButton({ if (notice.isEmpty() || confirming) onDelete() else confirming = true }) {
            Text("Delete")
        }
    }
}

/** The app type and profile a rule sets, as one line. */
private fun ruleSummary(
    rule: RecognitionRule,
    profiles: Map<WritingProfile, String>,
): String =
    listOfNotNull(
            rule.category?.let { appTypeLabels[it] },
            rule.profile?.let { profiles[it] },
        )
        .joinToString(" · ")

/** The desktop's built-in rules, read-only and folded away until asked for. */
@Composable
private fun BuiltInRules(profiles: Map<WritingProfile, String>) {
    var shown by rememberSaveable { mutableStateOf(false) }
    TextButton({ shown = !shown }, Modifier.padding(horizontal = 8.dp)) {
        Text(if (shown) "Hide built-in rules" else "Show ${builtInRules.size} built-in rules")
    }
    if (shown) {
        builtInRules.forEach { rule ->
            ListItem(
                headlineContent = { Text(rule.match) },
                supportingContent = { Text(ruleSummary(rule, profiles)) },
            )
        }
    }
}

/** One of the user's rules: its match text, app type and profile, editable, and delete. */
@Composable
private fun RuleEditor(
    rule: RecognitionRule,
    appTypes: Map<AppCategory?, String>,
    profiles: Map<WritingProfile?, String>,
    onEdit: (RecognitionRule) -> Unit,
    onDelete: () -> Unit,
) {
    Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            OutlinedTextField(
                rule.match,
                { onEdit(rule.copy(match = it)) },
                Modifier.weight(1f),
                label = { Text("App name or ID contains") },
                singleLine = true,
                isError = rule.match.isBlank(),
            )
            IconButton(onDelete) {
                Icon(
                    painterResource(R.drawable.ic_close),
                    contentDescription = "Delete ${rule.match}",
                )
            }
        }
        RuleChoices(rule.category, rule.profile, appTypes, profiles) { category, profile ->
            onEdit(rule.copy(category = category, profile = profile))
        }
    }
}

@Composable
private fun RuleChoices(
    category: AppCategory?,
    profile: WritingProfile?,
    appTypes: Map<AppCategory?, String>,
    profiles: Map<WritingProfile?, String>,
    onChange: (AppCategory?, WritingProfile?) -> Unit,
) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text("App type", style = MaterialTheme.typography.bodyMedium)
        Dropdown(appTypes, category) { onChange(it, profile) }
        Text("Profile", style = MaterialTheme.typography.bodyMedium)
        Dropdown(profiles, profile) { onChange(category, it) }
    }
}

/** A new rule, added once it has a match text and sets an app type or a profile. */
@Composable
private fun AddRule(
    appTypes: Map<AppCategory?, String>,
    profiles: Map<WritingProfile?, String>,
    onAdd: (RecognitionRule) -> Unit,
) {
    var match by rememberSaveable { mutableStateOf("") }
    var category by remember { mutableStateOf<AppCategory?>(null) }
    var profile by remember { mutableStateOf<WritingProfile?>(null) }
    Column(Modifier.padding(horizontal = 16.dp, vertical = 8.dp)) {
        OutlinedTextField(
            match,
            { match = it },
            Modifier.fillMaxWidth(),
            label = { Text("App name or ID contains") },
            singleLine = true,
        )
        RuleChoices(category, profile, appTypes, profiles) { nextCategory, nextProfile ->
            category = nextCategory
            profile = nextProfile
        }
        TextButton(
            {
                onAdd(RecognitionRule(match.trim(), category, profile))
                match = ""
                category = null
                profile = null
            },
            enabled = match.isNotBlank() && (category != null || profile != null),
        ) {
            Text("Add rule")
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
internal fun PasteCode(provider: Provider, onPasteCode: (String) -> Unit) {
    var code by rememberSaveable { mutableStateOf("") }
    Column(Modifier.padding(horizontal = 16.dp)) {
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
        Modifier.padding(start = 16.dp, end = 8.dp, top = 8.dp),
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
    Surface { Settings(settings, signedIn, {}, {}, {}, {}) }
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
internal fun SettingsRefinementOffPreview() =
    SettingsPreview(
        SpeecherSettings(transcriptionProvider = Provider.ChatGpt, refinementEnabled = false),
        setOf(Provider.ChatGpt),
    )
