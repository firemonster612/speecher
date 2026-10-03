package app.speecher.android.dictation

import app.speecher.protocol.AUTOMATIC_LANGUAGE
import app.speecher.protocol.claudeVoiceLanguages
import app.speecher.protocol.codexDictationLanguages
import java.util.Locale

/** The spoken languages the provider's speech service listens for, as codes, Automatic first. */
val Provider.spokenLanguages: List<String>
    get() = if (this == Provider.Claude) claudeVoiceLanguages else codexDictationLanguages

/** The language's English name, such as "German", or "Automatic". */
fun spokenLanguageName(code: String): String =
    if (code == AUTOMATIC_LANGUAGE) "Automatic"
    else Locale.forLanguageTag(code).getDisplayLanguage(Locale.ENGLISH)

/** The English name, then the native one in parentheses when it differs: "German (Deutsch)". */
fun spokenLanguageLabel(code: String): String {
    val name = spokenLanguageName(code)
    val locale = Locale.forLanguageTag(code)
    val native = locale.getDisplayLanguage(locale)
    return if (code == AUTOMATIC_LANGUAGE || native == name) name else "$name ($native)"
}

/**
 * Why [provider] can't take the saved [language], as Settings and the panel show it, or null when
 * it can. The saved language stays as it is; dictation never falls back to English.
 */
fun spokenLanguageMismatch(provider: Provider, language: String): String? =
    if (language in provider.spokenLanguages) null
    else
        "${provider.label} can't listen for ${spokenLanguageName(language)}. " +
            "Choose another spoken language."
