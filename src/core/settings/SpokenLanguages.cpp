#include "core/settings/SpokenLanguages.h"

#include "core/LocalModelCatalog.h"
#include "core/ProviderTable.h"

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

} // namespace

QStringList spokenLanguages(const SpeechSettings &speech)
{
    const std::optional<ProviderId> provider = parseProviderId(speech.providerId);
    if (provider == ProviderId::Claude) {
        return withAutomatic(claudeVoiceLanguages);
    }
    if (provider == ProviderId::Codex) {
        return withAutomatic(codexLanguages);
    }
    if (provider == ProviderId::Endpoint) {
        return withAutomatic(endpointLanguages());
    }
    if (provider == ProviderId::Local) {
        const LocalModel *model = findLocalModel(speech.local.modelId);
        if (!model) {
            return {};
        }
        return localModelListensFor(*model, QLatin1String(kAutomaticSpokenLanguage)) ? withAutomatic(model->languages)
                                                                                    : model->languages;
    }
    return {};
}

QString requestedSpokenLanguage(const QString &language)
{
    return language == QLatin1String(kAutomaticSpokenLanguage) ? QString() : language;
}

bool listensForSpokenLanguage(const SpeechSettings &speech)
{
    const LocalModel *model =
        parseProviderId(speech.providerId) == ProviderId::Local ? findLocalModel(speech.local.modelId) : nullptr;
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
        parseProviderId(speech.providerId) == ProviderId::Local ? findLocalModel(speech.local.modelId) : nullptr;
    const QString service = model ? model->name : serviceLabel;
    if (speech.language == QLatin1String(kAutomaticSpokenLanguage)) {
        return QStringLiteral("%1 can't detect the language. Choose the language you speak.").arg(service);
    }
    return QStringLiteral("%1 can't listen for %2. Choose another Spoken Language.")
        .arg(service, englishName(speech.language));
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
