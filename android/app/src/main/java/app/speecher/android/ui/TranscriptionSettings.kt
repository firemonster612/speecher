package app.speecher.android.ui

import androidx.compose.foundation.layout.padding
import androidx.compose.material3.ListItem
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import app.speecher.android.dictation.advice
import app.speecher.android.dictation.spokenLanguageLabel
import app.speecher.android.dictation.spokenLanguageMismatch
import app.speecher.android.dictation.spokenLanguageName
import app.speecher.android.dictation.spokenLanguages
import app.speecher.android.provider.Provider
import app.speecher.android.provider.SpeechProvider
import app.speecher.android.provider.hasBatchTranscription
import app.speecher.android.provider.label
import app.speecher.android.provider.resolveSpeech
import app.speecher.android.settings.SpeecherSettings
import app.speecher.protocol.AUTOMATIC_LANGUAGE

// The Transcription page: the speech provider, the spoken language, the speech options and the
// microphone test.

@Composable
internal fun TranscriptionSettings(
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

/** The provider's spoken languages as code to label: Automatic, then by English name. */
private fun spokenLanguageChoices(provider: SpeechProvider): Map<String, String> =
    provider.spokenLanguages
        .sortedWith(compareBy({ it != AUTOMATIC_LANGUAGE }, ::spokenLanguageName))
        .associateWith(::spokenLanguageLabel)

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
