#include "core/settings/SettingsCodecs.h"

#include "core/EndpointUrl.h"
#include "core/ProviderChain.h"
#include "core/settings/SettingsKeys.h"

#include "core/BindingProcessor.h"
#include "core/CliToolDiscovery.h"
#include "core/Vocabulary.h"
#include "core/VocabularyLimit.h"
#include "core/settings/CorrectionSettingsCodec.h"
#include "core/settings/VocabularySettingsCodec.h"

#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>

#include <algorithm>

namespace speecher {
namespace {

QString normalizedAudioCaptureMode(const QString &value)
{
    const QString mode = value.trimmed();
    if (mode == QStringLiteral("warm") || mode == QStringLiteral("always_open")) {
        return QStringLiteral("warm");
    }
    return QStringLiteral("on_demand");
}

AudioCaptureSettings normalizedAudioCaptureSettings(AudioCaptureSettings settings)
{
    settings.deviceId = settings.deviceId.trimmed();
    settings.mode = normalizedAudioCaptureMode(settings.mode);
    settings.preRollMs = std::clamp(settings.preRollMs, 0, 1500);
    settings.postRollMs = std::clamp(settings.postRollMs, 0, 1500);
    // A cold microphone routinely needs 100-300 ms to deliver its first samples
    // through PulseAudio, so anything shorter than half a second fails at the margin.
    settings.readinessTimeoutMs = std::clamp(settings.readinessTimeoutMs, 500, 3000);
    settings.vadThresholdPercent = std::clamp(settings.vadThresholdPercent, 1, 20);
    return settings;
}

QString normalizedEndpointPath(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return ServerSettings{}.path;
    }
    return trimmed.startsWith(QLatin1Char('/')) ? trimmed : QLatin1Char('/') + trimmed;
}

QString defaultRefinementProvider()
{
    if (CliToolDiscovery::isCodexInstalled()) {
        return providerKey(ProviderId::OpenAi);
    }
    if (CliToolDiscovery::isClaudeCodeInstalled()) {
        return providerKey(ProviderId::Anthropic);
    }
    return providerKey(ProviderId::OpenAi);
}

QString profileKey(ProviderRole role, const char *field)
{
    return QLatin1String(role == ProviderRole::Speech ? "speech" : "refinement") + QLatin1String(field);
}

// A Writing Profile's own provider for a role and its model. An id no chain
// can hold reads as the page's choice, and a model as the provider's own
// without a provider of the profile's to go with.
// Haiku 4.5, dated or not, has left the picker for Haiku 5.5.
QString replacedAnthropicModel(const QString &model)
{
    return model == QStringLiteral("claude-haiku-4-5") || model == QStringLiteral("claude-haiku-4-5-20251001")
        ? QStringLiteral("claude-haiku-5-5")
        : model;
}

std::optional<ServiceChoice> readProfileProvider(const QJsonObject &object, ProviderRole role)
{
    const QString provider = object.value(profileKey(role, "Provider")).toString();
    if (!isChainProviderId(role, provider)) {
        return std::nullopt;
    }
    ServiceChoice first{provider, object.value(profileKey(role, "Model")).toString().trimmed()};
    if (parseProviderId(provider) == ProviderId::Anthropic) {
        first.model = replacedAnthropicModel(first.model);
    }
    return first;
}

// Stored under the keys a profile's provider and model always had, so an
// older release reads them.
void writeProfileProvider(QJsonObject &object, ProviderRole role, const std::optional<ServiceChoice> &first)
{
    if (!first || !isChainProviderId(role, first->service)) {
        return;
    }
    object.insert(profileKey(role, "Provider"), first->service);
    if (!first->model.trimmed().isEmpty()) {
        object.insert(profileKey(role, "Model"), first->model.trimmed());
    }
}

bool isOpenAiEffort(const QString &effort)
{
    return effort == QStringLiteral("none") || effort == QStringLiteral("low") || effort == QStringLiteral("medium")
        || effort == QStringLiteral("high") || effort == QStringLiteral("xhigh");
}

bool isAnthropicEffort(const QString &effort)
{
    return effort == QStringLiteral("low") || effort == QStringLiteral("medium") || effort == QStringLiteral("high")
        || effort == QStringLiteral("xhigh") || effort == QStringLiteral("max");
}

bool isOpenAiSpeed(const QString &speed)
{
    return speed == QStringLiteral("standard") || speed == QStringLiteral("fast")
        || speed == QStringLiteral("ultrafast");
}

// ChatGPT's and Claude's choices with a model, an effort and a speed each
// can run, the defaults' where the stored one is missing or unknown.
ServiceOrder withValidRefinementChoices(ServiceOrder services)
{
    const ServiceOrder defaults = defaultRefinementServices();
    for (const ProviderId id : {ProviderId::OpenAi, ProviderId::Anthropic}) {
        const bool openAi = id == ProviderId::OpenAi;
        const ServiceChoice fallback = defaults.choice(providerKey(id));
        ServiceChoice &choice = services.choice(providerKey(id));
        choice.model = choice.model.trimmed();
        if (choice.model.isEmpty()) {
            choice.model = fallback.model;
        } else if (!openAi) {
            choice.model = replacedAnthropicModel(choice.model);
        }
        if (!(openAi ? isOpenAiEffort(choice.effort) : isAnthropicEffort(choice.effort))) {
            choice.effort = fallback.effort;
        }
        const bool knownSpeed = openAi ? isOpenAiSpeed(choice.speed)
                                       : choice.speed == QStringLiteral("standard") || choice.speed == QStringLiteral("fast");
        if (!knownSpeed) {
            choice.speed = fallback.speed;
        }
    }
    return services;
}

// A Service Order as stored: {"order": [...], "choices": {key: {"model",
// "language", "effort", "speed"}}}, empty fields left out.
QByteArray serviceOrderToJson(const ServiceOrder &services)
{
    QJsonObject choices;
    for (auto it = services.choices.cbegin(); it != services.choices.cend(); ++it) {
        QJsonObject choice;
        const auto put = [&choice](const char *name, const QString &value) {
            if (!value.isEmpty()) choice.insert(QLatin1String(name), value);
        };
        put("model", it->model.trimmed());
        put("language", it->language);
        put("effort", it->effort);
        put("speed", it->speed);
        choices.insert(it.key(), choice);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("order"), QJsonArray::fromStringList(services.order)},
                                     {QStringLiteral("choices"), choices}})
        .toJson(QJsonDocument::Compact);
}

// The stored order over defaults: a service the file has no choice for keeps
// its default one. Nothing stored, or something that isn't an order, reads
// as the defaults.
ServiceOrder serviceOrderFromJson(const QByteArray &encoded, ServiceOrder defaults)
{
    const QJsonObject object = QJsonDocument::fromJson(encoded).object();
    if (!object.value(QStringLiteral("order")).isArray()) {
        return defaults;
    }
    defaults.order.clear();
    for (const QJsonValue &id : object.value(QStringLiteral("order")).toArray()) {
        defaults.order.append(id.toString());
    }
    const QJsonObject choices = object.value(QStringLiteral("choices")).toObject();
    for (auto it = choices.constBegin(); it != choices.constEnd(); ++it) {
        const QJsonObject choice = it.value().toObject();
        const auto field = [&choice](const char *name) { return choice.value(QLatin1String(name)).toString(); };
        defaults.choices.insert(it.key(), {it.key(), field("model"), field("language"), field("effort"), field("speed")});
    }
    return defaults;
}

} // namespace

SettingsCodecs::SettingsCodecs()
    : m_settings(QString::fromLatin1(SettingsKeys::Organization),
                 SettingsKeys::application())
{
}

QVariant SettingsCodecs::value(const QString &key, const QVariant &fallback) const
{
    return m_settings.value(key, fallback);
}

bool SettingsCodecs::setupCompleted() const
{
    return value(SettingsKeys::SetupCompleted, false).toBool();
}

void SettingsCodecs::setSetupCompleted(bool value)
{
    m_settings.setValue(SettingsKeys::SetupCompleted, value);
}

bool SettingsCodecs::recordingConsentNoticeShown() const
{
    return value(SettingsKeys::RecordingConsentNoticeShown, false).toBool();
}

void SettingsCodecs::setRecordingConsentNoticeShown(bool value)
{
    m_settings.setValue(SettingsKeys::RecordingConsentNoticeShown, value);
}

bool SettingsCodecs::launchAtLogin() const
{
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    constexpr bool defaultValue = true;
#else
    constexpr bool defaultValue = false;
#endif
    return value(SettingsKeys::LaunchAtLogin, defaultValue).toBool();
}

void SettingsCodecs::setLaunchAtLogin(bool value)
{
    m_settings.setValue(SettingsKeys::LaunchAtLogin, value);
}

bool SettingsCodecs::transcriptionPreviewEnabled() const
{
    return value(SettingsKeys::UiTranscriptionPreviewEnabled, true).toBool();
}

void SettingsCodecs::setTranscriptionPreviewEnabled(bool value)
{
    m_settings.setValue(SettingsKeys::UiTranscriptionPreviewEnabled, value);
}

bool SettingsCodecs::refinementPreviewEnabled() const
{
    return value(SettingsKeys::UiRefinementPreviewEnabled, true).toBool();
}

void SettingsCodecs::setRefinementPreviewEnabled(bool value)
{
    m_settings.setValue(SettingsKeys::UiRefinementPreviewEnabled, value);
}

bool SettingsCodecs::selectionEditReviewEnabled() const
{
    return value(SettingsKeys::UiSelectionEditReviewEnabled, true).toBool();
}

void SettingsCodecs::setSelectionEditReviewEnabled(bool value)
{
    m_settings.setValue(SettingsKeys::UiSelectionEditReviewEnabled, value);
}

int SettingsCodecs::previewWords() const
{
    return std::clamp(value(SettingsKeys::UiPreviewWords, 7).toInt(), 1, 40);
}

void SettingsCodecs::setPreviewWords(int value)
{
    m_settings.setValue(SettingsKeys::UiPreviewWords, std::clamp(value, 1, 40));
}

QString SettingsCodecs::theme() const
{
    const QString theme = value(SettingsKeys::UiTheme, QStringLiteral("system")).toString();
    if (theme == QStringLiteral("light") || theme == QStringLiteral("dark")) {
        return theme;
    }
    return QStringLiteral("system");
}

void SettingsCodecs::setTheme(const QString &value)
{
    if (value == QStringLiteral("light") || value == QStringLiteral("dark")) {
        m_settings.setValue(SettingsKeys::UiTheme, value);
        return;
    }
    m_settings.setValue(SettingsKeys::UiTheme, QStringLiteral("system"));
}

// Any id: the UI owns its schemes, and one that is gone reads as its own.
QString SettingsCodecs::colorScheme() const
{
    const QString scheme = value(SettingsKeys::UiColorScheme, QStringLiteral("steno")).toString();
    return scheme.isEmpty() ? QStringLiteral("steno") : scheme;
}

void SettingsCodecs::setColorScheme(const QString &value)
{
    m_settings.setValue(SettingsKeys::UiColorScheme, value.trimmed());
}

bool SettingsCodecs::translucency() const
{
    return value(SettingsKeys::UiTranslucency, true).toBool();
}

void SettingsCodecs::setTranslucency(bool value)
{
    m_settings.setValue(SettingsKeys::UiTranslucency, value);
}

QString SettingsCodecs::windowLayout() const
{
    return value(SettingsKeys::UiWindowLayout, QString()).toString() == QStringLiteral("split") ? QStringLiteral("split")
                                                                                     : QStringLiteral("framed");
}

void SettingsCodecs::setWindowLayout(const QString &value)
{
    m_settings.setValue(SettingsKeys::UiWindowLayout,
                        value == QStringLiteral("split") ? QStringLiteral("split") : QStringLiteral("framed"));
}

bool SettingsCodecs::roundedCorners() const
{
    return value(SettingsKeys::UiRoundedCorners, true).toBool();
}

void SettingsCodecs::setRoundedCorners(bool value)
{
    m_settings.setValue(SettingsKeys::UiRoundedCorners, value);
}

bool SettingsCodecs::boringMode() const
{
    return value(SettingsKeys::UiBoringMode, false).toBool();
}

void SettingsCodecs::setBoringMode(bool value)
{
    m_settings.setValue(SettingsKeys::UiBoringMode, value);
}

bool SettingsCodecs::pauseMediaDuringTranscription() const
{
    return value(SettingsKeys::UiPauseMedia, true).toBool();
}

void SettingsCodecs::setPauseMediaDuringTranscription(bool value)
{
    m_settings.setValue(SettingsKeys::UiPauseMedia, value);
}

bool SettingsCodecs::soundsEnabled() const
{
    return value(SettingsKeys::UiSoundsEnabled, false).toBool();
}

void SettingsCodecs::setSoundsEnabled(bool value)
{
    m_settings.setValue(SettingsKeys::UiSoundsEnabled, value);
}

ServiceOrder SettingsCodecs::speechServices() const
{
    return normalizedServiceOrder(
        ProviderRole::Speech,
        serviceOrderFromJson(value(SettingsKeys::SpeechServices, QByteArray()).toByteArray(), defaultSpeechServices()));
}

void SettingsCodecs::setSpeechServices(const ServiceOrder &value)
{
    ServiceOrder services = normalizedServiceOrder(ProviderRole::Speech, value);
    // Another Local Model is a choice, which the hardware's suggestion no
    // longer replaces (LocalSpeechSettings::modelChosen).
    if (localModelId(SpeechSettings{services}) != localModelId(SpeechSettings{speechServices()})) {
        m_settings.setValue(SettingsKeys::LocalModelChosen, true);
    }
    if (services.order.isEmpty()) {
        services.order = defaultSpeechServices().order;
    }
    m_settings.setValue(SettingsKeys::SpeechServices, serviceOrderToJson(services));
}

QString SettingsCodecs::legacySpeechProvider() const
{
    const QString fallback = providerKey(ProviderId::Claude);
    const QString provider = value(SettingsKeys::SpeechProvider, fallback).toString();
    return provider.isEmpty() ? fallback : provider;
}

ServiceOrder SettingsCodecs::legacySpeechServices() const
{
    ServiceOrder services = defaultSpeechServices();
    services.order = QStringList{legacySpeechProvider()}
        + value(SettingsKeys::SpeechFallbackProviders, QStringList()).toStringList();
    // Settings saved before fallbacks had their own language have none.
    const QString prefix = SettingsKeys::SpeechFallbackLanguages + QLatin1Char('/');
    for (const QString &key : m_settings.allKeys()) {
        if (key.startsWith(prefix)) {
            services.choice(key.mid(prefix.size())).language = m_settings.value(key).toString();
        }
    }
    services.choice(providerKey(ProviderId::Local)).model =
        value(SettingsKeys::LocalModel, localModelId(SpeechSettings{})).toString();
    services.choice(providerKey(ProviderId::Endpoint)).model =
        value(SettingsKeys::SpeechEndpointModel, QString()).toString().trimmed();
    return normalizedServiceOrder(ProviderRole::Speech, services);
}

bool SettingsCodecs::codexFinalRetranscribe() const
{
    return value(SettingsKeys::CodexFinalRetranscribe, true).toBool();
}

void SettingsCodecs::setCodexFinalRetranscribe(bool value)
{
    m_settings.setValue(SettingsKeys::CodexFinalRetranscribe, value);
}

QString SettingsCodecs::spokenLanguage() const
{
    return value(SettingsKeys::SpokenLanguage, QStringLiteral("en")).toString();
}

void SettingsCodecs::setSpokenLanguage(const QString &value)
{
    m_settings.setValue(SettingsKeys::SpokenLanguage, value);
}

LocalSpeechSettings SettingsCodecs::localSpeechSettings() const
{
    const LocalSpeechSettings defaults;
    LocalSpeechSettings settings;
    settings.modelChosen = value(SettingsKeys::LocalModelChosen, m_settings.contains(SettingsKeys::LocalModel)).toBool();
    // A graphics card saved before the backend was chosen too has no backend
    // saved, and reads as Automatic on that card.
    settings.runsOn.backend = value(SettingsKeys::LocalBackend, defaults.runsOn.backend).toString();
    settings.runsOn.deviceId = value(SettingsKeys::LocalDevice, QString()).toString();
    settings.idleUnloadMinutes =
        std::max(0, value(SettingsKeys::LocalIdleUnloadMinutes, defaults.idleUnloadMinutes).toInt());
    const QString speedTestPrefix = SettingsKeys::LocalSpeedTest + QLatin1Char('/');
    for (const QString &key : m_settings.allKeys()) {
        if (!key.startsWith(speedTestPrefix)) {
            continue;
        }
        // Results saved before they were kept per Runs on choice ran on
        // Automatic, the only choice there was.
        QString speedKey = key.mid(speedTestPrefix.size());
        if (!speedKey.contains(QLatin1Char('/'))) {
            speedKey = localSpeedTestKey(speedKey, {});
        }
        settings.speedTestSeconds.insert(speedKey, m_settings.value(key).toDouble());
    }
    return settings;
}

void SettingsCodecs::setLocalSpeechSettings(const LocalSpeechSettings &value)
{
    const auto previous = localSpeechSettings();
    m_settings.setValue(SettingsKeys::LocalModelChosen, value.modelChosen || previous.modelChosen);
    m_settings.setValue(SettingsKeys::LocalBackend, value.runsOn.backend);
    m_settings.setValue(SettingsKeys::LocalDevice, value.runsOn.deviceId);
    m_settings.setValue(SettingsKeys::LocalIdleUnloadMinutes, std::max(0, value.idleUnloadMinutes));
    m_settings.remove(SettingsKeys::LocalSpeedTest);
    m_settings.beginGroup(SettingsKeys::LocalSpeedTest);
    for (auto it = value.speedTestSeconds.cbegin(); it != value.speedTestSeconds.cend(); ++it) {
        m_settings.setValue(it.key(), it.value());
    }
    m_settings.endGroup();
}

ServerSettings SettingsCodecs::speechServer() const
{
    ServerSettings settings;
    settings.baseUrl = endpointServerBase(value(SettingsKeys::SpeechEndpointBaseUrl, QString()).toString());
    settings.path = normalizedEndpointPath(value(SettingsKeys::SpeechEndpointPath, settings.path).toString());
    return settings;
}

void SettingsCodecs::setSpeechServer(const ServerSettings &value)
{
    m_settings.setValue(SettingsKeys::SpeechEndpointBaseUrl, endpointServerBase(value.baseUrl));
    m_settings.setValue(SettingsKeys::SpeechEndpointPath, normalizedEndpointPath(value.path));
}

ServerSettings SettingsCodecs::refinementServer() const
{
    ServerSettings settings;
    const QString format = value(SettingsKeys::RefinementEndpointFormat, settings.format).toString();
    settings.format = format == QStringLiteral("anthropic") ? format : QStringLiteral("openai");
    settings.baseUrl = withoutTrailingSlashes(value(SettingsKeys::RefinementEndpointBaseUrl, QString()).toString());
    return settings;
}

void SettingsCodecs::setRefinementServer(const ServerSettings &value)
{
    // Retired: which key the server sends follows from its address.
    m_settings.remove(SettingsKeys::RefinementEndpointUseCliproxyKey);
    m_settings.setValue(SettingsKeys::RefinementEndpointFormat,
                        value.format == QStringLiteral("anthropic") ? value.format : QStringLiteral("openai"));
    m_settings.setValue(SettingsKeys::RefinementEndpointBaseUrl, withoutTrailingSlashes(value.baseUrl));
}

LocalRunnerSettings SettingsCodecs::localRunnerSettings() const
{
    LocalRunnerSettings settings;
    const QString previousDefault = value(SettingsKeys::LocalRunnerModel, QString()).toString().isEmpty()
        ? QString() : QStringLiteral("ollama");
    const QString runner = value(SettingsKeys::LocalRunner, previousDefault).toString();
    if (runner == QStringLiteral("ollama") || runner == QStringLiteral("lmstudio") || runner == QStringLiteral("llama-server")) {
        settings.runner = runner;
    }
    return settings;
}

void SettingsCodecs::setLocalRunnerSettings(const LocalRunnerSettings &value)
{
    const bool known = value.runner == QStringLiteral("ollama") || value.runner == QStringLiteral("lmstudio")
        || value.runner == QStringLiteral("llama-server");
    m_settings.setValue(SettingsKeys::LocalRunner, known ? value.runner : QString());
}

// Terms lose the profiles the settings no longer hold, on the way in and out,
// so a profile recreated under a deleted one's id does not inherit its terms.
static QList<VocabularyEntry> withOfferedProfiles(QList<VocabularyEntry> entries,
                                                  const QList<WritingProfileSettings> &profiles)
{
    for (VocabularyEntry &entry : entries) {
        entry.profiles = offeredVocabularyProfiles(entry.profiles, profiles);
    }
    return entries;
}

QList<VocabularyEntry> SettingsCodecs::vocabularyEntries() const
{
    return withOfferedProfiles(VocabularySettingsCodec::load(m_settings), writingProfileSettings());
}

void SettingsCodecs::setVocabularyEntries(const QList<VocabularyEntry> &entries)
{
    VocabularySettingsCodec::store(m_settings, withOfferedProfiles(entries, writingProfileSettings()));
}

bool SettingsCodecs::recordVocabularyUsage(const QString &text)
{
    return VocabularySettingsCodec::recordUsage(m_settings, text);
}

std::optional<QStringList> SettingsCodecs::addVocabularyTerms(const QStringList &terms)
{
    // Its own QSettings, because one keeps reporting a failed sync forever:
    // after one failed add, every later one would report a failure too.
    SettingsCodecs settings;
    // Two `vocabulary add`s with no app running would each read the list, and
    // the second to write would drop the first one's terms. Windows keeps the
    // settings in the registry, so its lock goes in the per-user temp folder.
    // Not <file>.lock, which QSettings takes for itself while it syncs.
#ifdef Q_OS_WIN
    QLockFile lock(QDir::temp().filePath(SettingsKeys::application() + QStringLiteral("-vocabulary.lock")));
#else
    QLockFile lock(settings.m_settings.fileName() + QStringLiteral(".vocabulary.lock"));
#endif
    if (!lock.tryLock(5000)) {
        return std::nullopt;
    }
    // Re-reads what another process saved since this one opened the settings.
    settings.m_settings.sync();
    const QList<VocabularyEntry> saved = settings.vocabularyEntries();
    QList<VocabularyEntry> entries = saved;
    QStringList held;
    for (const QString &term : terms) {
        const QString cleaned = term.simplified();
        // A term limited to some Writing Profiles counts too: adding it again
        // for every profile would list it twice.
        const bool listed = std::any_of(entries.cbegin(), entries.cend(), [&cleaned](const VocabularyEntry &entry) {
            return entry.term.compare(cleaned, Qt::CaseInsensitive) == 0;
        });
        if (listed) {
            held.append(term);
        } else {
            entries.append({cleaned});
        }
    }
    if (held.size() == terms.size()) {
        return held;
    }
    settings.setVocabularyEntries(entries);
    settings.m_settings.sync();
    if (settings.m_settings.status() != QSettings::NoError) {
        // Every QSettings on the file shares its unsaved values, so the
        // running app would show and use terms that were never saved.
        settings.setVocabularyEntries(saved);
        return std::nullopt;
    }
    return held;
}

AudioCaptureSettings SettingsCodecs::audioCaptureSettings() const
{
    return normalizedAudioCaptureSettings({
        value(SettingsKeys::AudioDeviceId, QString()).toString(),
        value(SettingsKeys::AudioCaptureMode, QStringLiteral("on_demand")).toString(),
        value(SettingsKeys::AudioVadEnabled, false).toBool(),
        value(SettingsKeys::AudioPreRollMs, 250).toInt(),
        value(SettingsKeys::AudioPostRollMs, 200).toInt(),
        value(SettingsKeys::AudioReadinessTimeoutMs, 900).toInt(),
        value(SettingsKeys::AudioVadThresholdPercent, 2).toInt(),
    });
}

void SettingsCodecs::setAudioCaptureSettings(const AudioCaptureSettings &value)
{
    const AudioCaptureSettings settings = normalizedAudioCaptureSettings(value);
    m_settings.setValue(SettingsKeys::AudioDeviceId, settings.deviceId);
    m_settings.setValue(SettingsKeys::AudioCaptureMode, settings.mode);
    m_settings.setValue(SettingsKeys::AudioVadEnabled, settings.vadEnabled);
    m_settings.setValue(SettingsKeys::AudioPreRollMs, settings.preRollMs);
    m_settings.setValue(SettingsKeys::AudioPostRollMs, settings.postRollMs);
    m_settings.setValue(SettingsKeys::AudioReadinessTimeoutMs, settings.readinessTimeoutMs);
    m_settings.setValue(SettingsKeys::AudioVadThresholdPercent, settings.vadThresholdPercent);
}

// A rule pointing at a deleted profile loses the profile, here where settings
// are read and written; the callers then drop a rule left with nothing to set.
static AppRecognitionRule withoutDeletedProfile(AppRecognitionRule rule,
                                                const QList<WritingProfileSettings> &profiles)
{
    if (rule.writingProfile && offeredWritingProfile(*rule.writingProfile, profiles, QString()).isEmpty()) {
        rule.writingProfile.reset();
    }
    return rule;
}

QList<AppRecognitionRule> SettingsCodecs::appRecognitionRules() const
{
    const QList<WritingProfileSettings> profiles = writingProfileSettings();
    const QJsonDocument document = QJsonDocument::fromJson(
        value(SettingsKeys::AppRecognitionRules, QByteArray()).toByteArray());
    QList<AppRecognitionRule> rules;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        AppRecognitionRule rule;
        rule.match = object.value(QStringLiteral("match")).toString().trimmed();
        const QString category = object.value(QStringLiteral("category")).toString();
        if (!category.isEmpty()) {
            const AppCategory parsed = appCategoryFromName(category);
            if (parsed != AppCategory::Unknown) {
                rule.category = parsed;
            }
        }
        const QString profile = object.value(QStringLiteral("writingProfile")).toString();
        if (!profile.isEmpty()) {
            rule.writingProfile = writingProfileFromName(profile);
        }
        rule = withoutDeletedProfile(rule, profiles);
        if (!rule.match.isEmpty() && (rule.category || rule.writingProfile)) {
            rules.append(rule);
        }
    }
    return rules;
}

void SettingsCodecs::setAppRecognitionRules(const QList<AppRecognitionRule> &rules)
{
    const QList<WritingProfileSettings> profiles = writingProfileSettings();
    QJsonArray array;
    for (const AppRecognitionRule &stored : rules) {
        const AppRecognitionRule rule = withoutDeletedProfile(stored, profiles);
        const QString match = rule.match.trimmed();
        if (match.isEmpty() || (!rule.category && !rule.writingProfile)) {
            continue;
        }
        QJsonObject object{{QStringLiteral("match"), match}};
        if (rule.category) {
            object.insert(QStringLiteral("category"), appCategoryName(*rule.category));
        }
        if (rule.writingProfile) {
            object.insert(QStringLiteral("writingProfile"), *rule.writingProfile);
        }
        array.append(object);
    }
    m_settings.setValue(SettingsKeys::AppRecognitionRules,
                        QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QList<BindingRule> SettingsCodecs::bindingRules() const
{
    const QString stored = value(SettingsKeys::BindingRules, QString()).toString();
    const QJsonDocument document = QJsonDocument::fromJson(stored.toUtf8());
    if (!document.isArray()) {
        return {};
    }

    QList<BindingRule> rules;
    const QJsonArray array = document.array();
    rules.reserve(array.size());
    for (const QJsonValue &value : array) {
        const QJsonObject object = value.toObject();
        const QString phrase = object.value(QStringLiteral("phrase")).toString().trimmed();
        const QString replacement = object.value(QStringLiteral("replacement")).toString();
        if (!phrase.isEmpty() && !replacement.trimmed().isEmpty()) {
            rules.append({phrase, replacement});
        }
    }
    return BindingProcessor::validateRules(rules).rules;
}

bool SettingsCodecs::setBindingRules(const QList<BindingRule> &rules, QString *error)
{
    const BindingValidationResult validated = BindingProcessor::validateRules(rules);
    if (!validated.ok()) {
        if (error) {
            *error = validated.messages().join(QStringLiteral("\n"));
        }
        return false;
    }

    QJsonArray array;
    for (const BindingRule &rule : validated.rules) {
        array.append(QJsonObject{
            {QStringLiteral("phrase"), rule.phrase},
            {QStringLiteral("replacement"), rule.replacement},
        });
    }
    m_settings.setValue(SettingsKeys::BindingRules,
                        QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    return true;
}

bool SettingsCodecs::correctionLearningEnabled() const
{
    return CorrectionSettingsCodec::learningEnabled(m_settings);
}

void SettingsCodecs::setCorrectionLearningEnabled(bool enabled)
{
    CorrectionSettingsCodec::storeLearningEnabled(m_settings, enabled);
}

bool SettingsCodecs::insightsEnabled() const
{
    return value(SettingsKeys::InsightsEnabled, true).toBool();
}

void SettingsCodecs::setInsightsEnabled(bool enabled)
{
    m_settings.setValue(SettingsKeys::InsightsEnabled, enabled);
}

QList<LearnedCorrection> SettingsCodecs::learnedCorrections() const
{
    return CorrectionSettingsCodec::load(m_settings);
}

void SettingsCodecs::setLearnedCorrections(const QList<LearnedCorrection> &corrections)
{
    CorrectionSettingsCodec::store(m_settings, corrections);
}

bool SettingsCodecs::refinementEnabled() const
{
    return value(SettingsKeys::RefinementEnabled, true).toBool();
}

void SettingsCodecs::setRefinementEnabled(bool value)
{
    m_settings.setValue(SettingsKeys::RefinementEnabled, value);
}

ServiceOrder SettingsCodecs::refinementServices() const
{
    ServiceOrder defaults = defaultRefinementServices();
    if (!m_settings.contains(SettingsKeys::RefinementServices)) {
        defaults.order = {defaultRefinementProvider()};
    }
    return withValidRefinementChoices(normalizedServiceOrder(
        ProviderRole::Refinement,
        serviceOrderFromJson(value(SettingsKeys::RefinementServices, QByteArray()).toByteArray(), defaults)));
}

void SettingsCodecs::setRefinementServices(const ServiceOrder &value)
{
    ServiceOrder services = withValidRefinementChoices(normalizedServiceOrder(ProviderRole::Refinement, value));
    // Saving a snapshot that still holds the default leaves it a default,
    // which the setup assistant may replace; only a different order is a
    // choice.
    if (!m_settings.contains(SettingsKeys::RefinementServices) && services == refinementServices()) return;
    if (services.order.isEmpty()) {
        services.order = {defaultRefinementProvider()};
    }
    m_settings.setValue(SettingsKeys::RefinementServices, serviceOrderToJson(services));
}

ServiceOrder SettingsCodecs::legacyRefinementServices(bool *enabled) const
{
    const QString key = SettingsKeys::RefinementProvider;
    const QString provider = m_settings.contains(key) ? value(key, providerKey(ProviderId::OpenAi)).toString()
                                                      : defaultRefinementProvider();
    *enabled = provider != kNoRefinementProvider;
    ServiceOrder services = defaultRefinementServices();
    // None kept no order; Clean up my words turned on again starts from the
    // default.
    services.order = *enabled ? QStringList{provider}
            + value(SettingsKeys::RefinementFallbackProviders, QStringList()).toStringList()
                              : QStringList{defaultRefinementProvider()};
    services.choice(providerKey(ProviderId::OpenAi)) = {providerKey(ProviderId::OpenAi), legacyOpenAiModel(),
                                                        {}, legacyOpenAiEffort(), legacyOpenAiSpeed()};
    services.choice(providerKey(ProviderId::Anthropic)) = {
        providerKey(ProviderId::Anthropic), legacyAnthropicModel(), {}, legacyAnthropicEffort(),
        value(SettingsKeys::AnthropicFastMode, true).toBool() ? QStringLiteral("fast") : QStringLiteral("standard")};
    services.choice(providerKey(ProviderId::Local)).model =
        value(SettingsKeys::LocalRunnerModel, QString()).toString().trimmed();
    services.choice(providerKey(ProviderId::Endpoint)).model =
        value(SettingsKeys::RefinementEndpointModel, QString()).toString().trimmed();
    services = normalizedServiceOrder(ProviderRole::Refinement, services);
    if (services.order.isEmpty()) {
        services.order = {defaultRefinementProvider()};
    }
    return withValidRefinementChoices(services);
}

QString SettingsCodecs::refinementStyle() const
{
    const QString style = value(SettingsKeys::RefinementStyle, QStringLiteral("balanced")).toString();
    if (style == QStringLiteral("strong_polish") || style == QStringLiteral("balanced") || style == QStringLiteral("light_cleanup")) {
        return style;
    }
    return QStringLiteral("balanced");
}

QString SettingsCodecs::defaultWritingProfile() const
{
    const QString profile = value(SettingsKeys::DefaultWritingProfile, WritingProfile::Other).toString();
    return offeredWritingProfile(writingProfileFromName(profile), writingProfileSettings());
}

void SettingsCodecs::setDefaultWritingProfile(const QString &value)
{
    m_settings.setValue(SettingsKeys::DefaultWritingProfile,
                        offeredWritingProfile(writingProfileFromName(value), writingProfileSettings()));
}

// The id when it is built in or one of the custom choices, otherwise fallback.
template<typename Custom>
static QString offeredChoice(const QString &value, const QStringList &builtIn, const QList<Custom> &custom,
                             const QString &fallback)
{
    const QString id = value.trimmed().toLower();
    const bool offered = builtIn.contains(id)
        || std::any_of(custom.cbegin(), custom.cend(), [&id](const Custom &choice) { return choice.id == id; });
    return offered ? id : fallback;
}

// A profile whose tone or level was deleted falls back to no tone override or
// Medium, here where settings are read and written, so no stored profile
// names one that is gone.
static QString cleanupStrength(const QString &value, const QList<CustomCleanupLevel> &levels)
{
    return offeredChoice(value, kBuiltInCleanupLevels, levels, QStringLiteral("balanced"));
}

static QString writingTone(const QString &value, const QList<CustomTone> &tones)
{
    return offeredChoice(value, kBuiltInTones, tones, QStringLiteral("none"));
}

// The profiles the user added, which follow the built-ins in stored order.
static QList<WritingProfileSettings> customWritingProfiles(const QList<WritingProfileSettings> &profiles)
{
    QList<WritingProfileSettings> custom;
    for (const WritingProfileSettings &profile : profiles) {
        if (profile.profile.startsWith(kCustomIdPrefix)) {
            custom.append(profile);
        }
    }
    return custom;
}

QList<WritingProfileSettings> SettingsCodecs::writingProfileSettings() const
{
    const QByteArray encoded = value(SettingsKeys::WritingProfiles, QByteArray()).toByteArray();
    if (encoded.isEmpty()) {
        QList<WritingProfileSettings> defaults = defaultWritingProfileSettings();
        const QString legacyStrength = refinementStyle();
        for (WritingProfileSettings &settings : defaults) {
            settings.cleanupStrength = legacyStrength;
        }
        return defaults;
    }

    const QJsonDocument document = QJsonDocument::fromJson(encoded);
    if (!document.isArray()) {
        return defaultWritingProfileSettings();
    }
    const QList<CustomCleanupLevel> levels = customCleanupLevels();
    const QList<CustomTone> tones = customTones();
    QList<WritingProfileSettings> settings;
    for (const QJsonValue &item : document.array()) {
        const QJsonObject object = item.toObject();
        settings.append({
            writingProfileFromName(object.value(QStringLiteral("profile")).toString()),
            cleanupStrength(object.value(QStringLiteral("cleanupStrength")).toString(), levels),
            writingTone(object.value(QStringLiteral("tone")).toString(), tones),
            object.value(QStringLiteral("instructions")).toString(),
            object.value(QStringLiteral("name")).toString(),
            object.value(QStringLiteral("outputLanguage")).toString(),
        });
        settings.last().speechFirst = readProfileProvider(object, ProviderRole::Speech);
        settings.last().refinementFirst = readProfileProvider(object, ProviderRole::Refinement);
    }
    bool hasAiCoding = false;
    for (const WritingProfileSettings &entry : settings) {
        if (entry.profile == WritingProfile::AiCoding) {
            hasAiCoding = true;
            break;
        }
    }
    QList<WritingProfileSettings> complete;
    for (const WritingProfileSettings &fallback : defaultWritingProfileSettings()) {
        WritingProfileSettings resolved = writingProfileSettingsFor(settings, fallback.profile);
        // Settings saved before the AI coding profile existed inherit Work's
        // tuning, since AI coding apps used the Work profile back then.
        if (fallback.profile == WritingProfile::AiCoding && !hasAiCoding) {
            resolved = writingProfileSettingsFor(settings, WritingProfile::Work);
            resolved.profile = WritingProfile::AiCoding;
        }
        complete.append(resolved);
    }
    return complete + customWritingProfiles(settings);
}

void SettingsCodecs::setWritingProfileSettings(const QList<WritingProfileSettings> &value)
{
    const QList<CustomCleanupLevel> levels = customCleanupLevels();
    const QList<CustomTone> tones = customTones();
    QList<WritingProfileSettings> profiles;
    for (const WritingProfileSettings &fallback : defaultWritingProfileSettings()) {
        profiles.append(writingProfileSettingsFor(value, fallback.profile));
    }
    QJsonArray array;
    for (const WritingProfileSettings &settings : profiles + customWritingProfiles(value)) {
        QJsonObject object{
            {QStringLiteral("profile"), settings.profile},
            {QStringLiteral("cleanupStrength"), cleanupStrength(settings.cleanupStrength, levels)},
            {QStringLiteral("tone"), writingTone(settings.tone, tones)},
            {QStringLiteral("instructions"), settings.instructions},
        };
        if (!isBuiltInWritingProfile(settings.profile)) {
            object.insert(QStringLiteral("name"), settings.name);
        }
        if (!settings.outputLanguage.isEmpty()) {
            object.insert(QStringLiteral("outputLanguage"), settings.outputLanguage);
        }
        writeProfileProvider(object, ProviderRole::Speech, settings.speechFirst);
        writeProfileProvider(object, ProviderRole::Refinement, settings.refinementFirst);
        array.append(object);
    }
    m_settings.setValue(SettingsKeys::WritingProfiles,
                        QJsonDocument(array).toJson(QJsonDocument::Compact));
    // A deleted profile leaves the stored terms it limited now, not only
    // when they are next read.
    setVocabularyEntries(vocabularyEntries());
}

QList<WritingProfileOverride> SettingsCodecs::writingProfileOverrides() const
{
    const QJsonDocument document = QJsonDocument::fromJson(
        value(SettingsKeys::WritingProfileOverrides, QByteArray()).toByteArray());
    QList<WritingProfileOverride> overrides;
    for (const QJsonValue &item : document.array()) {
        const QJsonObject object = item.toObject();
        const QString applicationId = object.value(QStringLiteral("applicationId")).toString().trimmed();
        if (!applicationId.isEmpty()) {
            overrides.append({
                applicationId,
                writingProfileFromName(object.value(QStringLiteral("profile")).toString()),
                object.value(QStringLiteral("enabled")).toBool(true),
            });
        }
    }
    return overrides;
}

void SettingsCodecs::setWritingProfileOverrides(const QList<WritingProfileOverride> &value)
{
    QJsonArray array;
    for (const WritingProfileOverride &override : value) {
        const QString applicationId = override.applicationId.trimmed();
        if (applicationId.isEmpty()) {
            continue;
        }
        array.append(QJsonObject{
            {QStringLiteral("applicationId"), applicationId},
            {QStringLiteral("profile"), override.profile},
            {QStringLiteral("enabled"), override.enabled},
        });
    }
    m_settings.setValue(SettingsKeys::WritingProfileOverrides,
                        QJsonDocument(array).toJson(QJsonDocument::Compact));
}

bool SettingsCodecs::useTargetContext() const
{
    return value(SettingsKeys::UseTargetContext, true).toBool();
}

void SettingsCodecs::setUseTargetContext(bool value)
{
    m_settings.setValue(SettingsKeys::UseTargetContext, value);
}

bool SettingsCodecs::includeScreenshotContext() const
{
    return value(SettingsKeys::IncludeScreenshotContext, false).toBool();
}

void SettingsCodecs::setIncludeScreenshotContext(bool value)
{
    m_settings.setValue(SettingsKeys::IncludeScreenshotContext, value);
}

QString SettingsCodecs::additionalInstructions() const
{
    return value(SettingsKeys::AdditionalInstructions, QString()).toString();
}

void SettingsCodecs::setAdditionalInstructions(const QString &value)
{
    m_settings.setValue(SettingsKeys::AdditionalInstructions, value);
}

bool SettingsCodecs::customSystemPromptEnabled() const
{
    return value(SettingsKeys::CustomSystemPromptEnabled, false).toBool();
}

void SettingsCodecs::setCustomSystemPromptEnabled(bool value)
{
    m_settings.setValue(SettingsKeys::CustomSystemPromptEnabled, value);
}

QString SettingsCodecs::customSystemPrompt() const
{
    return value(SettingsKeys::CustomSystemPrompt, QString()).toString();
}

void SettingsCodecs::setCustomSystemPrompt(const QString &value)
{
    m_settings.setValue(SettingsKeys::CustomSystemPrompt, value);
}

QList<CustomTone> SettingsCodecs::customTones() const
{
    QList<CustomTone> tones;
    const QJsonArray array =
        QJsonDocument::fromJson(value(SettingsKeys::CustomTones, QByteArray()).toByteArray()).array();
    for (const QJsonValue &item : array) {
        const QJsonObject object = item.toObject();
        tones.append({object.value(QStringLiteral("id")).toString(),
                      object.value(QStringLiteral("name")).toString(),
                      object.value(QStringLiteral("instruction")).toString()});
    }
    return tones;
}

void SettingsCodecs::setCustomTones(const QList<CustomTone> &value)
{
    QJsonArray array;
    for (const CustomTone &tone : value) {
        array.append(QJsonObject{{QStringLiteral("id"), tone.id},
                                 {QStringLiteral("name"), tone.name},
                                 {QStringLiteral("instruction"), tone.instruction}});
    }
    m_settings.setValue(SettingsKeys::CustomTones, QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QList<CustomCleanupLevel> SettingsCodecs::customCleanupLevels() const
{
    QList<CustomCleanupLevel> levels;
    const QJsonArray array = QJsonDocument::fromJson(
                                 value(SettingsKeys::CustomCleanupLevels, QByteArray()).toByteArray())
                                 .array();
    for (const QJsonValue &item : array) {
        const QJsonObject object = item.toObject();
        levels.append({object.value(QStringLiteral("id")).toString(),
                       object.value(QStringLiteral("name")).toString(),
                       object.value(QStringLiteral("base")).toString(),
                       object.value(QStringLiteral("instructions")).toString()});
    }
    return levels;
}

void SettingsCodecs::setCustomCleanupLevels(const QList<CustomCleanupLevel> &value)
{
    QJsonArray array;
    for (const CustomCleanupLevel &level : value) {
        array.append(QJsonObject{{QStringLiteral("id"), level.id},
                                 {QStringLiteral("name"), level.name},
                                 {QStringLiteral("base"), level.base},
                                 {QStringLiteral("instructions"), level.instructions}});
    }
    m_settings.setValue(SettingsKeys::CustomCleanupLevels,
                        QJsonDocument(array).toJson(QJsonDocument::Compact));
}

QString SettingsCodecs::legacyOpenAiModel() const
{
    const QString model = value(SettingsKeys::OpenAiModel, QStringLiteral("gpt-6-luna")).toString().trimmed();
    return model.isEmpty() ? QStringLiteral("gpt-6-luna") : model;
}

OpenAiAuthMode SettingsCodecs::openAiAuthMode() const
{
    const QString mode = value(SettingsKeys::OpenAiAuthMode, QString()).toString();
    // Names earlier releases stored; their codex_then_api_key reads as Auto
    // with any other unknown name.
    if (mode == QStringLiteral("api_key_env")) {
        return OpenAiAuthMode::Env;
    }
    if (mode == QStringLiteral("api_key_settings")) {
        return OpenAiAuthMode::Settings;
    }
    return openAiAuthModeFromName(mode);
}

void SettingsCodecs::setOpenAiAuthMode(OpenAiAuthMode value)
{
    m_settings.setValue(SettingsKeys::OpenAiAuthMode, openAiAuthModeName(value));
}

QString SettingsCodecs::legacyOpenAiEffort() const
{
    const QString effort = value(SettingsKeys::OpenAiEffort, QStringLiteral("none")).toString();
    return isOpenAiEffort(effort) ? effort : QStringLiteral("none");
}

QString SettingsCodecs::legacyOpenAiSpeed() const
{
    const QString legacy = value(SettingsKeys::OpenAiFastMode, true).toBool()
        ? QStringLiteral("fast")
        : QStringLiteral("standard");
    const QString speed = value(SettingsKeys::OpenAiSpeed, legacy).toString();
    return isOpenAiSpeed(speed) ? speed : legacy;
}

QString SettingsCodecs::legacyAnthropicModel() const
{
    const QString model = value(SettingsKeys::AnthropicModel, QStringLiteral("claude-opus-5-5")).toString().trimmed();
    if (model.isEmpty()) {
        return QStringLiteral("claude-opus-5-5");
    }
    return replacedAnthropicModel(model);
}

AnthropicAuthMode SettingsCodecs::anthropicAuthMode() const
{
    return anthropicAuthModeFromName(value(SettingsKeys::AnthropicAuthMode, QString()).toString());
}

void SettingsCodecs::setAnthropicAuthMode(AnthropicAuthMode value)
{
    m_settings.setValue(SettingsKeys::AnthropicAuthMode, anthropicAuthModeName(value));
}

QString SettingsCodecs::legacyAnthropicEffort() const
{
    const QString effort = value(SettingsKeys::AnthropicEffort, QStringLiteral("low")).toString();
    return isAnthropicEffort(effort) ? effort : QStringLiteral("low");
}

DeliverySteps SettingsCodecs::deliverySteps() const
{
    const DeliverySteps steps{
        .insert = value(SettingsKeys::InsertThroughAccessibility, true).toBool(),
        .press = value(SettingsKeys::PressPasteKeys, true).toBool(),
        .keep = value(SettingsKeys::KeepOnClipboard, true).toBool(),
    };
    // A file edited by hand to turn every step off still delivers somehow.
    return steps.any() ? steps : DeliverySteps{.insert = false, .press = false, .keep = true};
}

void SettingsCodecs::setDeliverySteps(const DeliverySteps &value)
{
    m_settings.setValue(SettingsKeys::InsertThroughAccessibility, value.insert);
    m_settings.setValue(SettingsKeys::PressPasteKeys, value.press);
    m_settings.setValue(SettingsKeys::KeepOnClipboard, value.keep);
}

OutputFormat SettingsCodecs::outputFormat() const
{
    return outputFormatFromString(value(SettingsKeys::OutputFormat, QStringLiteral("plain")).toString())
        .value_or(OutputFormat::PlainText);
}

void SettingsCodecs::setOutputFormat(OutputFormat value)
{
    m_settings.setValue(SettingsKeys::OutputFormat, outputFormatName(value));
}

bool SettingsCodecs::ydotoolEnabled() const
{
    return value(SettingsKeys::YdotoolEnabled, false).toBool();
}

void SettingsCodecs::setYdotoolEnabled(bool value)
{
    m_settings.setValue(SettingsKeys::YdotoolEnabled, value);
}

int SettingsCodecs::completionStatusDurationMs() const
{
    return std::clamp(value(SettingsKeys::CompletionStatusDurationMs, 500).toInt(), 0, 5000);
}

void SettingsCodecs::setCompletionStatusDurationMs(int value)
{
    m_settings.setValue(SettingsKeys::CompletionStatusDurationMs, std::clamp(value, 0, 5000));
}

QList<PasteRule> pasteRulesFromJson(const QByteArray &encoded)
{
    if (encoded.isEmpty()) {
        return defaultPasteRules();
    }
    const QJsonDocument document = QJsonDocument::fromJson(encoded);
    if (!document.isArray()) {
        return defaultPasteRules();
    }

    QList<PasteRule> rules;
    for (const QJsonValue &value : document.array()) {
        const QJsonObject object = value.toObject();
        PasteRule rule;
        rule.scope = pasteRuleScopeFromName(object.value(QStringLiteral("scope")).toString());
        rule.match = object.value(QStringLiteral("match")).toString().trimmed();
        rule.method = pasteMethodFromName(object.value(QStringLiteral("method")).toString());
        rule.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        rules.append(rule);
    }
    return rules;
}

QByteArray pasteRulesToJson(const QList<PasteRule> &rules)
{
    QJsonArray array;
    for (const PasteRule &rule : rules) {
        array.append(QJsonObject{
            {QStringLiteral("scope"), pasteRuleScopeName(rule.scope)},
            {QStringLiteral("match"), rule.match.trimmed()},
            {QStringLiteral("method"), pasteMethodName(rule.method)},
            {QStringLiteral("enabled"), rule.enabled},
        });
    }
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QList<PasteRule> SettingsCodecs::pasteRules() const
{
    QList<PasteRule> rules = pasteRulesFromJson(value(SettingsKeys::PasteRules, QByteArray()).toByteArray());
    // The delivery steps are the global rule now; migrateDeliverySteps read
    // any that was stored.
    rules.removeIf([](const PasteRule &rule) { return rule.scope == PasteRuleScope::Global; });
    return rules;
}

void SettingsCodecs::setPasteRules(const QList<PasteRule> &rules)
{
    m_settings.setValue(SettingsKeys::PasteRules, pasteRulesToJson(rules));
}

ShortcutActivationMode SettingsCodecs::shortcutActivationMode() const
{
    return shortcutActivationModeFromName(
        value(SettingsKeys::ShortcutActivationMode, QStringLiteral("hybrid")).toString());
}

void SettingsCodecs::setShortcutActivationMode(ShortcutActivationMode value)
{
    m_settings.setValue(SettingsKeys::ShortcutActivationMode, shortcutActivationModeName(value));
}

namespace {

QString globalShortcutKey(GlobalShortcutRole role)
{
    switch (role) {
    case GlobalShortcutRole::Cancel:
        return SettingsKeys::CancelShortcut;
    case GlobalShortcutRole::Pause:
        return SettingsKeys::PauseShortcut;
    case GlobalShortcutRole::Dictation:
        break;
    }
    return SettingsKeys::GlobalShortcut;
}

QString portalShortcutClearedKey(GlobalShortcutRole role)
{
    return globalShortcutKey(role) + QStringLiteral("PortalCleared");
}

} // namespace

ShortcutBinding SettingsCodecs::globalShortcut(GlobalShortcutRole role) const
{
    return ShortcutBinding::fromString(value(globalShortcutKey(role), QString()).toString());
}

void SettingsCodecs::setGlobalShortcut(GlobalShortcutRole role, const ShortcutBinding &value)
{
    m_settings.setValue(globalShortcutKey(role), value.toString());
}

bool SettingsCodecs::globalShortcutCleared(GlobalShortcutRole role) const
{
    return m_settings.contains(globalShortcutKey(role)) && globalShortcut(role).isEmpty();
}

void SettingsCodecs::forgetGlobalShortcutCombination(GlobalShortcutRole role)
{
    if (!globalShortcut(role).isSingleKey()) {
        m_settings.remove(globalShortcutKey(role));
    }
}

void SettingsCodecs::forgetGlobalShortcutSingleKey(GlobalShortcutRole role)
{
    if (globalShortcut(role).isSingleKey()) {
        m_settings.remove(globalShortcutKey(role));
    }
}

bool SettingsCodecs::portalShortcutCleared(GlobalShortcutRole role) const
{
    return value(portalShortcutClearedKey(role), false).toBool();
}

void SettingsCodecs::setPortalShortcutCleared(GlobalShortcutRole role, bool value)
{
    if (value) {
        m_settings.setValue(portalShortcutClearedKey(role), true);
    } else {
        m_settings.remove(portalShortcutClearedKey(role));
    }
}

UpdateChannel SettingsCodecs::updateChannel() const
{
    return updateChannelFromName(
        value(SettingsKeys::UpdatesChannel, QStringLiteral("stable")).toString());
}

void SettingsCodecs::setUpdateChannel(UpdateChannel value)
{
    m_settings.setValue(SettingsKeys::UpdatesChannel, updateChannelName(value));
}

bool SettingsCodecs::autoCheckUpdates() const
{
    return value(SettingsKeys::UpdatesAutoCheck, true).toBool();
}

void SettingsCodecs::setAutoCheckUpdates(bool value)
{
    m_settings.setValue(SettingsKeys::UpdatesAutoCheck, value);
}

bool SettingsCodecs::autoInstallUpdates() const
{
    return value(SettingsKeys::UpdatesAutoInstall, false).toBool();
}

void SettingsCodecs::setAutoInstallUpdates(bool value)
{
    m_settings.setValue(SettingsKeys::UpdatesAutoInstall, value);
}

int SettingsCodecs::updateCheckIntervalMinutes() const
{
    // A hand-edited zero or negative interval would make the check timer spin.
    return qBound(UpdateSettings::minimumCheckIntervalMinutes,
                  value(SettingsKeys::UpdatesCheckIntervalMinutes, 30).toInt(),
                  UpdateSettings::maximumCheckIntervalMinutes);
}

void SettingsCodecs::setUpdateCheckIntervalMinutes(int value)
{
    m_settings.setValue(SettingsKeys::UpdatesCheckIntervalMinutes, value);
}

QString SettingsCodecs::updateCheckIntervalUnit() const
{
    return value(SettingsKeys::UpdatesCheckIntervalUnit, QString()).toString();
}

void SettingsCodecs::setUpdateCheckIntervalUnit(const QString &value)
{
    m_settings.setValue(SettingsKeys::UpdatesCheckIntervalUnit, value);
}

namespace {

// Stored as the "settings" token earlier releases write, which may sit in a
// list with "listening", so a restart from one of them reopens the window too.
const QString reopenMainWindowToken = QStringLiteral("settings");

} // namespace

bool SettingsCodecs::updatesReopenMainWindow() const
{
    return value(SettingsKeys::UpdatesRestoreState, QString()).toString().contains(reopenMainWindowToken);
}

void SettingsCodecs::setUpdatesReopenMainWindow(bool value)
{
    if (value) {
        m_settings.setValue(SettingsKeys::UpdatesRestoreState, reopenMainWindowToken);
        m_settings.setValue(SettingsKeys::UpdatesRestoreStateTime,
                            QDateTime::currentMSecsSinceEpoch());
    } else {
        m_settings.remove(SettingsKeys::UpdatesRestoreState);
        m_settings.remove(SettingsKeys::UpdatesRestoreStateTime);
    }
}

qint64 SettingsCodecs::updatesReopenMainWindowTime() const
{
    return value(SettingsKeys::UpdatesRestoreStateTime, 0).toLongLong();
}

qint64 SettingsCodecs::updatesLastCheckTime() const
{
    return value(SettingsKeys::UpdatesLastCheckTime, 0).toLongLong();
}

void SettingsCodecs::setUpdatesLastCheckTime(qint64 value)
{
    m_settings.setValue(SettingsKeys::UpdatesLastCheckTime, value);
}

QString SettingsCodecs::updatesDismissedVersion() const
{
    return value(SettingsKeys::UpdatesDismissedVersion, QString()).toString();
}

void SettingsCodecs::setUpdatesDismissedVersion(const QString &value)
{
    m_settings.setValue(SettingsKeys::UpdatesDismissedVersion, value);
}

QString SettingsCodecs::updatesLastRunVersion() const
{
    return value(SettingsKeys::UpdatesLastRunVersion, QString()).toString();
}

void SettingsCodecs::setUpdatesLastRunVersion(const QString &value)
{
    m_settings.setValue(SettingsKeys::UpdatesLastRunVersion, value);
}

qint64 SettingsCodecs::updatesLastRunBuildNumber() const
{
    return value(SettingsKeys::UpdatesLastRunBuildNumber, -1).toLongLong();
}

void SettingsCodecs::setUpdatesLastRunBuildNumber(qint64 value)
{
    m_settings.setValue(SettingsKeys::UpdatesLastRunBuildNumber, value);
}

QString SettingsCodecs::updatesPendingWhatsNewVersion() const
{
    return value(SettingsKeys::UpdatesPendingWhatsNewVersion, QString()).toString();
}

void SettingsCodecs::setUpdatesPendingWhatsNewVersion(const QString &value)
{
    m_settings.setValue(SettingsKeys::UpdatesPendingWhatsNewVersion, value);
}

QString SettingsCodecs::openAiCliproxyAccount() const
{
    return value(SettingsKeys::OpenAiCliproxyAccount, QString()).toString().trimmed();
}

void SettingsCodecs::setOpenAiCliproxyAccount(const QString &value)
{
    m_settings.setValue(SettingsKeys::OpenAiCliproxyAccount, value.trimmed());
}

QString SettingsCodecs::anthropicCliproxyAccount() const
{
    return value(SettingsKeys::AnthropicCliproxyAccount, QString()).toString().trimmed();
}

void SettingsCodecs::setAnthropicCliproxyAccount(const QString &value)
{
    m_settings.setValue(SettingsKeys::AnthropicCliproxyAccount, value.trimmed());
}

QString SettingsCodecs::cliproxyOauthDir() const
{
    const QString configured = value(SettingsKeys::CliproxyOauthDir, QString()).toString().trimmed();
    if (!configured.isEmpty()) {
        return configured;
    }
    // CLI Proxy API's stock auth-dir is ~/.cli-proxy-api; installs may configure
    // another location. Prefer whichever candidate actually holds account files.
    const QStringList candidates{
        QDir::homePath() + QStringLiteral("/.cli-proxy-api"),
        QDir::homePath() + QStringLiteral("/.local/share/cliproxy-api/oauth"),
    };
    for (const QString &candidate : candidates) {
        if (!QDir(candidate).entryList({QStringLiteral("claude-*.json"), QStringLiteral("codex-*.json")},
                                       QDir::Files).isEmpty()) {
            return candidate;
        }
    }
    for (const QString &candidate : candidates) {
        if (QDir(candidate).exists()) {
            return candidate;
        }
    }
    return candidates.first();
}

QString SettingsCodecs::configuredCliproxyOauthDir() const
{
    return value(SettingsKeys::CliproxyOauthDir, QString()).toString().trimmed();
}

void SettingsCodecs::setCliproxyOauthDir(const QString &value)
{
    m_settings.setValue(SettingsKeys::CliproxyOauthDir, value.trimmed());
}

QString SettingsCodecs::cliproxyBaseUrl() const
{
    return endpointServerBase(value(SettingsKeys::CliproxyBaseUrl, QString()).toString());
}

void SettingsCodecs::setCliproxyBaseUrl(const QString &value)
{
    m_settings.setValue(SettingsKeys::CliproxyBaseUrl, endpointServerBase(value));
}

void SettingsCodecs::migrateServiceOrder()
{
    constexpr int currentMigrationVersion = 1;
    if (value(SettingsKeys::ServiceOrderMigrationVersion, 0).toInt() >= currentMigrationVersion) {
        return;
    }
    m_settings.setValue(SettingsKeys::ServiceOrderMigrationVersion, currentMigrationVersion);
    const auto storedAny = [this](std::initializer_list<QString> keys) {
        return std::any_of(keys.begin(), keys.end(), [this](const QString &key) { return m_settings.contains(key); });
    };
    const bool fallbackLanguages = std::ranges::any_of(m_settings.allKeys(), [](const QString &key) {
        return key.startsWith(SettingsKeys::SpeechFallbackLanguages + QLatin1Char('/'));
    });
    if (!m_settings.contains(SettingsKeys::SpeechServices)
        && (fallbackLanguages
            || storedAny({SettingsKeys::SpeechProvider, SettingsKeys::SpeechFallbackProviders, SettingsKeys::LocalModel,
                          SettingsKeys::SpeechEndpointModel}))) {
        setSpeechServices(legacySpeechServices());
    }
    if (!m_settings.contains(SettingsKeys::RefinementServices)
        && storedAny({SettingsKeys::RefinementProvider, SettingsKeys::RefinementFallbackProviders,
                      SettingsKeys::OpenAiModel, SettingsKeys::OpenAiEffort, SettingsKeys::OpenAiSpeed,
                      SettingsKeys::OpenAiFastMode, SettingsKeys::AnthropicModel, SettingsKeys::AnthropicEffort,
                      SettingsKeys::AnthropicFastMode, SettingsKeys::LocalRunnerModel,
                      SettingsKeys::RefinementEndpointModel})) {
        bool enabled = true;
        // Written as it is, even when it matches the default: the person's
        // saved order is a choice.
        m_settings.setValue(SettingsKeys::RefinementServices, serviceOrderToJson(legacyRefinementServices(&enabled)));
        setRefinementEnabled(enabled);
    }
    // The CLI Proxy preset becomes the server at the proxy's address with no
    // key of its own, which sends the proxy's (usesProxyKey). A key kept for
    // a Custom server entered before is let go, so it doesn't go to the
    // proxy; the preset stays for an older release.
    if (value(SettingsKeys::RefinementEndpointPreset, QString()).toString() == QStringLiteral("cliproxy")) {
        m_settings.setValue(SettingsKeys::RefinementEndpointBaseUrl, endpointApiBase(cliproxyBaseUrl()));
        m_settings.remove(SettingsKeys::RefinementEndpointApiKey);
        QStringList inKeyring = value(SettingsKeys::SecretsInKeyring, QStringList()).toStringList();
        if (inKeyring.removeAll(QStringLiteral("refinement-endpoint-key")) > 0) {
            m_settings.setValue(SettingsKeys::SecretsInKeyring, inKeyring);
        }
    }
}

QString defaultClaudeCredentialsPath()
{
    QString configDir = qEnvironmentVariable("CLAUDE_CONFIG_DIR");
    if (configDir.isEmpty()) configDir = QDir::homePath() + QStringLiteral("/.claude");
    return QDir(configDir).filePath(QStringLiteral(".credentials.json"));
}

QString SettingsCodecs::claudeCredentialsPath() const
{
    return value(SettingsKeys::ClaudeCredentialsPath, defaultClaudeCredentialsPath()).toString();
}

QString SettingsCodecs::claudeEndpointBase() const
{
    return value(SettingsKeys::ClaudeEndpointBase, QStringLiteral("https://claude.ai")).toString();
}

QString SettingsCodecs::claudeVoicePath() const
{
    return value(SettingsKeys::ClaudeVoicePath, QStringLiteral("/api/ws/speech_to_text/voice_stream")).toString();
}

} // namespace speecher
