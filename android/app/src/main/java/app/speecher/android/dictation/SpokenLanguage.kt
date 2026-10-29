package app.speecher.android.dictation

import app.speecher.protocol.AUTOMATIC_LANGUAGE
import app.speecher.protocol.claudeVoiceLanguages
import app.speecher.protocol.codexDictationLanguages
import app.speecher.protocol.endpointSpeechLanguages
import java.util.Locale

/** The spoken languages the provider's speech service listens for, as codes, Automatic first. */
val SpeechProvider.spokenLanguages: List<String>
    get() =
        when (this) {
            SpeechProvider.ChatGpt -> codexDictationLanguages
            SpeechProvider.Claude -> claudeVoiceLanguages
            SpeechProvider.Endpoint -> endpointSpeechLanguages
        }

/** The language's English name, such as "German", or "Automatic". */
fun spokenLanguageName(code: String): String =
    if (code == AUTOMATIC_LANGUAGE) "Automatic" else locale(code).getDisplayLanguage(Locale.ENGLISH)

/** The English name, then the native one in parentheses when it differs: "German (Deutsch)". */
fun spokenLanguageLabel(code: String): String {
    val name = spokenLanguageName(code)
    val locale = locale(code)
    val native = locale.getDisplayLanguage(locale)
    return if (code == AUTOMATIC_LANGUAGE || native == name) name else "$name ($native)"
}

// Whisper's code for Javanese predates the standard one.
private fun locale(code: String): Locale = Locale.forLanguageTag(if (code == "jw") "jv" else code)

/**
 * The failure dictating with [provider] in the saved [language] meets, or null when the provider
 * listens for it. The saved language stays as it is; dictation never falls back to English.
 */
fun spokenLanguageMismatch(
    provider: SpeechProvider,
    language: String,
): FailureReason.SpokenLanguage? =
    if (language in provider.spokenLanguages) null
    else FailureReason.SpokenLanguage(provider, language)

/** What went wrong and what to do, as Settings and the panel show it. */
val FailureReason.SpokenLanguage.advice: String
    get() =
        "${provider.label} can't listen for ${spokenLanguageName(language)}. " +
            "Choose another spoken language."
