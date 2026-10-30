package app.speecher.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
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
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.provider.OpenAiSpeed
import app.speecher.android.provider.Provider
import app.speecher.android.provider.RefinementChoice
import app.speecher.android.provider.cleanup
import app.speecher.android.provider.defaultRefinement
import app.speecher.android.provider.label
import app.speecher.android.provider.providerOrder
import app.speecher.android.provider.refinementEfforts
import app.speecher.android.provider.refinementModels
import app.speecher.android.provider.resolveSignedIn
import app.speecher.android.settings.SpeecherSettings
import app.speecher.protocol.modelSupportsFastMode
import app.speecher.protocol.modelSupportsUltrafast

// The Refinement page: whether Insert refined is offered, the provider, its model, thinking and
// speed, the fallback profile and the instructions every refinement gets.

@Composable
internal fun RefinementSettings(
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
