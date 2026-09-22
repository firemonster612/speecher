package app.speecher.android.ui

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
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
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.R
import app.speecher.android.dictation.Provider
import app.speecher.android.dictation.SpeecherSettings

/** Settings. Every change goes out whole through [onChange]; the caller persists it. */
@Composable
fun Settings(
    settings: SpeecherSettings,
    signedIn: Set<Provider>,
    onChange: (SpeecherSettings) -> Unit,
    onSignIn: (Provider) -> Unit,
    onSignOut: (Provider) -> Unit,
    modifier: Modifier = Modifier,
) {
    val rowColors = ListItemDefaults.colors(containerColor = MaterialTheme.colorScheme.surface)
    Column(modifier) {
        Section("Transcription")
        ProviderPicker(settings.transcriptionProvider) {
            onChange(settings.copy(transcriptionProvider = it))
        }

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
            ProviderPicker(settings.refinementProvider) {
                onChange(settings.copy(refinementProvider = it))
            }
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
        Provider.entries.forEach { provider ->
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
    }
}

@Composable
private fun ProviderPicker(selected: Provider, onSelect: (Provider) -> Unit) {
    SingleChoiceSegmentedButtonRow(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 8.dp)
    ) {
        Provider.entries.forEachIndexed { index, provider ->
            SegmentedButton(
                selected = provider == selected,
                onClick = { onSelect(provider) },
                shape = SegmentedButtonDefaults.itemShape(index, Provider.entries.size),
            ) {
                Text(provider.label)
            }
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
internal fun SettingsRefinementOffPreview() =
    SettingsPreview(
        SpeecherSettings(transcriptionProvider = Provider.ChatGpt, refinementEnabled = false),
        setOf(Provider.ChatGpt),
    )
