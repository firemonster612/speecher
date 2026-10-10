#include "core/settings/SpokenLanguages.h"

#include "core/LocalModelCatalog.h"

#include <QLocale>

namespace speecher {
namespace {

// The codes Claude Voice accepted in the 2026-10-03 probe. It refuses any
// other, including "multi", by closing the stream; leaving the code out
// detects the language.
const QStringList claudeVoiceLanguages = languageCodes(
    "am ar be bg bn bs ca cs da de el en es et fa fi fr he hi hr hu id it ja kn ko lt lv mk mr ms nl no pl "
    "pt ro ru sk sl sr sv sw ta te th tl tr uk ur vi yue zh");

// The codes ChatGPT's dictation stream accepted in the same probe; it detects
// the language when given none.
const QStringList codexLanguages = languageCodes(
    "af am ar az be bg bn bs ca cs cy da de el en es et fa fi fr gl gu he hi hr hu hy id is it ja ka kk kn "
    "ko lt lv mi mk ml mn mr ms my ne nl no pl pt ro ru sk sl so sr sv sw ta te th tl tr uk ur vi yue zh");

// A Custom Endpoint's server is unknown; most speak Whisper's languages.
QStringList endpointLanguages()
{
    return findLocalModel(QStringLiteral("whisper-turbo"))->languages;
}

QStringList withAutomatic(QStringList languages)
{
    languages.append(QLatin1String(kAutomaticSpokenLanguage));
    return languages;
}

// Whisper's code for Javanese predates the standard one.
QLocale::Language localeLanguage(const QString &language)
{
    return QLocale::codeToLanguage(language == QStringLiteral("jw") ? QStringLiteral("jv") : language);
}

QString englishName(const QString &language)
{
    const QLocale::Language known = localeLanguage(language);
    return known == QLocale::AnyLanguage ? language : QLocale::languageToString(known);
}

QString nativeLanguageName(QLocale::Language language)
{
    switch (language) {
    case QLocale::English:
        return {};
    // Qt names these by their default territory or script: "español de
    // España", "简体中文".
    case QLocale::Spanish:
        return QStringLiteral("español");
    case QLocale::Chinese:
        return QStringLiteral("中文");
    default:
        break;
    }
    const QLocale locale(language);
    return locale.language() == language ? locale.nativeLanguageName() : QString();
}

} // namespace

QStringList spokenLanguages(const SpeechSettings &speech)
{
    if (speech.providerId == QStringLiteral("claude")) {
        return withAutomatic(claudeVoiceLanguages);
    }
    if (speech.providerId == QStringLiteral("codex")) {
        return withAutomatic(codexLanguages);
    }
    if (speech.providerId == QStringLiteral("endpoint")) {
        return withAutomatic(endpointLanguages());
    }
    if (speech.providerId == QStringLiteral("local")) {
        const LocalModel *model = findLocalModel(speech.local.modelId);
        if (!model) {
            return {};
        }
        return localModelListensFor(*model, QLatin1String(kAutomaticSpokenLanguage)) ? withAutomatic(model->languages)
                                                                                    : model->languages;
    }
    return {};
}

QString spokenLanguageLabel(const QString &language)
{
    if (language == QLatin1String(kAutomaticSpokenLanguage)) {
        return QStringLiteral("Automatic");
    }
    const QString english = englishName(language);
    const QString native = nativeLanguageName(localeLanguage(language));
    if (native.isEmpty() || native.compare(english, Qt::CaseInsensitive) == 0) {
        return english;
    }
    return QStringLiteral("%1 (%2)").arg(english, native);
}

QString requestedSpokenLanguage(const QString &language)
{
    return language == QLatin1String(kAutomaticSpokenLanguage) ? QString() : language;
}

bool listensForSpokenLanguage(const SpeechSettings &speech)
{
    const LocalModel *model =
        speech.providerId == QStringLiteral("local") ? findLocalModel(speech.local.modelId) : nullptr;
    if (model) {
        return localModelListensFor(*model, speech.language);
    }
    const QStringList offered = spokenLanguages(speech);
    return offered.isEmpty() || offered.contains(speech.language);
}

QString spokenLanguageProblem(const SpeechSettings &speech, const QString &serviceLabel)
{
    if (listensForSpokenLanguage(speech)) {
        return {};
    }
    const LocalModel *model =
        speech.providerId == QStringLiteral("local") ? findLocalModel(speech.local.modelId) : nullptr;
    const QString service = model ? model->name : serviceLabel;
    if (speech.language == QLatin1String(kAutomaticSpokenLanguage)) {
        return QStringLiteral("%1 can't detect the language. Choose the language you speak.").arg(service);
    }
    return QStringLiteral("%1 can't listen for %2. Choose another Spoken Language.")
        .arg(service, englishName(speech.language));
}

QString profileSpokenLanguageProblem(const SpeechSettings &profileSpeech,
                                     const QString &serviceLabel,
                                     const QString &instead)
{
    if (listensForSpokenLanguage(profileSpeech)) {
        return {};
    }
    const LocalModel *model = profileSpeech.providerId == QStringLiteral("local")
        ? findLocalModel(profileSpeech.local.modelId)
        : nullptr;
    const QString service = model ? model->name : serviceLabel;
    if (profileSpeech.language == QLatin1String(kAutomaticSpokenLanguage)) {
        return QStringLiteral("%1 can't detect the language, so this profile uses %2.").arg(service, instead);
    }
    return QStringLiteral("%1 doesn't listen for %2, your Spoken Language, so this profile uses %3.")
        .arg(service, englishName(profileSpeech.language), instead);
}

QStringList knownSpokenLanguages()
{
    QStringList languages = withAutomatic(claudeVoiceLanguages + codexLanguages);
    for (const LocalModel &model : localModelCatalog()) {
        languages += model.languages;
    }
    languages.removeDuplicates();
    languages.sort();
    return languages;
}

bool isKnownSpokenLanguage(const QString &language)
{
    return knownSpokenLanguages().contains(language);
}

} // namespace speecher
