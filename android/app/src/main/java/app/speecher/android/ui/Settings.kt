package app.speecher.android.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
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
import app.speecher.android.dictation.providerOrder
import app.speecher.android.dictation.refinementEfforts
import app.speecher.android.dictation.refinementModels
import app.speecher.protocol.CleanupStrength
import app.speecher.protocol.Tone
import app.speecher.protocol.WritingProfile
import app.speecher.protocol.WritingProfileSettings

// Labels from the desktop's SettingsSchema.cpp, in its order.
private val profileLabels =
    mapOf(
        WritingProfile.Work to "Work",
        WritingProfile.Email to "Email",
        WritingProfile.Personal to "Personal",
        WritingProfile.AiCoding to "AI coding",
        WritingProfile.Other to "Other",
    )

private val cleanupLabels =
    mapOf(
        CleanupStrength.None to "None",
        CleanupStrength.LightCleanup to "Light",
        CleanupStrength.Balanced to "Medium",
        CleanupStrength.StrongPolish to "High",
    )

private val toneLabels =
    mapOf(
        Tone.None to "No tone override",
        Tone.Formal to "Formal",
        Tone.Casual to "Casual",
        Tone.VeryCasual to "Very casual",
        Tone.Excited to "Excited",
        Tone.GenZ to "Gen Z",
    )

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
            ListItem(
                headlineContent = { Text("Fallback profile") },
                supportingContent = {
                    Text("Writing profile used when the target app does not imply one.")
                },
                trailingContent = {
                    Dropdown(profileLabels, settings.defaultWritingProfile) {
                        onChange(settings.copy(defaultWritingProfile = it))
                    }
                },
                colors = rowColors,
            )
            ListItem(
                headlineContent = { Text("Context") },
                supportingContent = { Text("Send the target app's context to the refiner") },
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
                                "it to the refiner. Never stored."
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
                            "Sends a screenshot of the app you're dictating into to the refiner. " +
                                "Needs a vision-capable refinement model, such as Claude Sonnet 5."
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
                "Choose cleanup strength and an optional explicit tone for each automatically " +
                    "detected profile.",
                Modifier.padding(horizontal = 16.dp),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            profileLabels.forEach { (profile, label) ->
                val behavior = settings.writingProfiles.getValue(profile)
                fun update(next: WritingProfileSettings) =
                    onChange(
                        settings.copy(
                            writingProfiles = settings.writingProfiles + (profile to next)
                        )
                    )
                ListItem(
                    headlineContent = { Text(label) },
                    supportingContent = {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Dropdown(cleanupLabels, behavior.cleanupStrength) {
                                update(behavior.copy(cleanupStrength = it))
                            }
                            Dropdown(toneLabels, behavior.tone) { update(behavior.copy(tone = it)) }
                        }
                    },
                    colors = rowColors,
                )
            }
        }

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
            "Names and terms the transcriber should spell your way.",
            Modifier.padding(horizontal = 16.dp),
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        settings.vocabulary.forEach { word ->
            ListItem(
                headlineContent = { Text(word) },
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
        if (signingIn != null) PasteCode(onPasteCode)
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

@Composable
internal fun PasteCode(onPasteCode: (String) -> Unit) {
    var expanded by rememberSaveable { mutableStateOf(false) }
    var code by rememberSaveable { mutableStateOf("") }
    if (!expanded) {
        TextButton({ expanded = true }, Modifier.padding(start = 16.dp)) {
            Text("Paste the link instead")
        }
        return
    }
    Column(Modifier.padding(horizontal = 16.dp)) {
        Text(
            "If the browser can't return on its own, copy its address bar after you approve and " +
                "paste it here.",
            style = MaterialTheme.typography.bodySmall,
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
