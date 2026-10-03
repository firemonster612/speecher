#pragma once

#include "core/AppSettings.h"

#include <QString>
#include <QStringList>

namespace speecher {

// The Spoken Languages the chosen speech service, or the chosen Local Model,
// listens for, as language codes in no particular order, with
// kAutomaticSpokenLanguage among them where it detects the language itself.
// docs/research/0007-speech-languages.md has where each list comes from.
// Empty for a service with no list, such as the test stub, which takes any.
QStringList spokenLanguages(const SpeechSettings &speech);

// "German (Deutsch)": the English name, then the native one when it differs.
// "Automatic" for kAutomaticSpokenLanguage.
QString spokenLanguageLabel(const QString &language);

// The language code a speech request names, or empty for Automatic, which
// names none.
QString requestedSpokenLanguage(const QString &language);

// Empty while the chosen service listens for the stored Spoken Language;
// otherwise the sentence that says it does not, naming the language in
// English and the service by serviceLabel, or a Local Model by its own name.
// Speecher never switches the language itself.
QString spokenLanguageProblem(const SpeechSettings &speech, const QString &serviceLabel);

// A code some service or Local Model offers, or kAutomaticSpokenLanguage:
// what `--language` accepts.
bool isKnownSpokenLanguage(const QString &language);

} // namespace speecher
