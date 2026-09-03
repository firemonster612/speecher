#include "core/settings/SettingsCodecs.h"
#include "core/settings/SettingsKeys.h"

#include "core/BindingProcessor.h"
#include "core/CliToolDiscovery.h"
#include "core/OutputMethod.h"
#include "core/settings/CorrectionSettingsCodec.h"
#include "core/settings/VocabularySettingsCodec.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

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
    settings.readinessTimeoutMs = std::clamp(settings.readinessTimeoutMs, 150, 3000);
    settings.vadThresholdPercent = std::clamp(settings.vadThresholdPercent, 1, 20);
    return settings;
}

QString defaultRefinementProvider()
{
    if (CliToolDiscovery::isCodexInstalled()) {
        return QStringLiteral("openai");
    }
    if (CliToolDiscovery::isClaudeCodeInstalled()) {
        return QStringLiteral("anthropic");
    }
    return QStringLiteral("openai");
}

} // namespace

SettingsCodecs::SettingsCodecs()
    : m_settings(QString::fromLatin1(SettingsKeys::Organization),
                 QString::fromLatin1(SettingsKeys::Application))
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

bool SettingsCodecs::launchAtLogin() const
{
#ifdef Q_OS_MACOS
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

QString SettingsCodecs::speechProvider() const
{
    const QString provider = value(SettingsKeys::SpeechProvider, QStringLiteral("claude")).toString();
    return provider.isEmpty() ? QStringLiteral("claude") : provider;
}

void SettingsCodecs::setSpeechProvider(const QString &value)
{
    m_settings.setValue(SettingsKeys::SpeechProvider, value.isEmpty() ? QStringLiteral("claude") : value);
}

QStringList SettingsCodecs::customVocabulary() const
{
    QStringList terms;
    for (const VocabularyEntry &entry : vocabularyEntries()) {
        terms.append(entry.term);
    }
    return terms;
}

void SettingsCodecs::setCustomVocabulary(const QStringList &value)
{
    QList<VocabularyEntry> entries;
    for (const QString &term : value) {
        entries.append({term, QStringLiteral("manual"), false, 0, 0});
    }
    setVocabularyEntries(entries);
}

QList<VocabularyEntry> SettingsCodecs::vocabularyEntries() const
{
    return VocabularySettingsCodec::load(m_settings);
}

void SettingsCodecs::setVocabularyEntries(const QList<VocabularyEntry> &entries)
{
    VocabularySettingsCodec::store(m_settings, entries);
}

void SettingsCodecs::recordVocabularyUsage(const QString &text)
{
    VocabularySettingsCodec::recordUsage(m_settings, text);
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

QList<AppRecognitionRule> SettingsCodecs::appRecognitionRules() const
{
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
        if (!rule.match.isEmpty() && (rule.category || rule.writingProfile)) {
            rules.append(rule);
        }
    }
    return rules;
}

void SettingsCodecs::setAppRecognitionRules(const QList<AppRecognitionRule> &rules)
{
    QJsonArray array;
    for (const AppRecognitionRule &rule : rules) {
        const QString match = rule.match.trimmed();
        if (match.isEmpty() || (!rule.category && !rule.writingProfile)) {
            continue;
        }
        QJsonObject object{{QStringLiteral("match"), match}};
        if (rule.category) {
            object.insert(QStringLiteral("category"), appCategoryName(*rule.category));
        }
        if (rule.writingProfile) {
            object.insert(QStringLiteral("writingProfile"), writingProfileName(*rule.writingProfile));
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

QList<LearnedCorrection> SettingsCodecs::learnedCorrections() const
{
    return CorrectionSettingsCodec::load(m_settings);
}

void SettingsCodecs::setLearnedCorrections(const QList<LearnedCorrection> &corrections)
{
    CorrectionSettingsCodec::store(m_settings, corrections);
}

void SettingsCodecs::setLearnedCorrectionEnabled(const QString &id, bool enabled)
{
    CorrectionSettingsCodec::setEnabled(m_settings, id, enabled);
}

void SettingsCodecs::removeLearnedCorrection(const QString &id)
{
    CorrectionSettingsCodec::remove(m_settings, id);
}

QString SettingsCodecs::refinementProvider() const
{
    const QString key = SettingsKeys::RefinementProvider;
    const QString provider = m_settings.contains(key) ? value(key, QStringLiteral("openai")).toString()
                                                      : defaultRefinementProvider();
    if (provider == QStringLiteral("none") || provider == QStringLiteral("anthropic")) {
        return provider;
    }
    return QStringLiteral("openai");
}

void SettingsCodecs::setRefinementProvider(const QString &value)
{
    if (value == QStringLiteral("none") || value == QStringLiteral("anthropic")) {
        m_settings.setValue(SettingsKeys::RefinementProvider, value);
        return;
    }
    m_settings.setValue(SettingsKeys::RefinementProvider, QStringLiteral("openai"));
}

QString SettingsCodecs::refinementStyle() const
{
    const QString style = value(SettingsKeys::RefinementStyle, QStringLiteral("balanced")).toString();
    if (style == QStringLiteral("strong_polish") || style == QStringLiteral("balanced") || style == QStringLiteral("light_cleanup")) {
        return style;
    }
    return QStringLiteral("balanced");
}

void SettingsCodecs::setRefinementStyle(const QString &value)
{
    if (value == QStringLiteral("strong_polish") || value == QStringLiteral("light_cleanup")) {
        m_settings.setValue(SettingsKeys::RefinementStyle, value);
        return;
    }
    m_settings.setValue(SettingsKeys::RefinementStyle, QStringLiteral("balanced"));
}

QString SettingsCodecs::defaultWritingProfile() const
{
    const QString profile = value(SettingsKeys::DefaultWritingProfile, QStringLiteral("other")).toString();
    return writingProfileName(writingProfileFromName(profile));
}

void SettingsCodecs::setDefaultWritingProfile(const QString &value)
{
    m_settings.setValue(
        SettingsKeys::DefaultWritingProfile,
        writingProfileName(writingProfileFromName(value)));
}

static QString cleanupStrength(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("none")
        || normalized == QStringLiteral("light_cleanup")
        || normalized == QStringLiteral("balanced")
        || normalized == QStringLiteral("strong_polish")) {
        return normalized;
    }
    return QStringLiteral("balanced");
}

static QString writingTone(const QString &value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("formal")
        || normalized == QStringLiteral("casual")
        || normalized == QStringLiteral("very_casual")
        || normalized == QStringLiteral("excited")
        || normalized == QStringLiteral("gen_z")) {
        return normalized;
    }
    return QStringLiteral("none");
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
    QList<WritingProfileSettings> settings;
    for (const QJsonValue &item : document.array()) {
        const QJsonObject object = item.toObject();
        settings.append({
            writingProfileFromName(object.value(QStringLiteral("profile")).toString()),
            cleanupStrength(object.value(QStringLiteral("cleanupStrength")).toString()),
            writingTone(object.value(QStringLiteral("tone")).toString()),
        });
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
    return complete;
}

void SettingsCodecs::setWritingProfileSettings(const QList<WritingProfileSettings> &value)
{
    QJsonArray array;
    for (const WritingProfileSettings &fallback : defaultWritingProfileSettings()) {
        const WritingProfileSettings settings = writingProfileSettingsFor(value, fallback.profile);
        array.append(QJsonObject{
            {QStringLiteral("profile"), writingProfileName(fallback.profile)},
            {QStringLiteral("cleanupStrength"), cleanupStrength(settings.cleanupStrength)},
            {QStringLiteral("tone"), writingTone(settings.tone)},
        });
    }
    m_settings.setValue(SettingsKeys::WritingProfiles,
                        QJsonDocument(array).toJson(QJsonDocument::Compact));
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
            {QStringLiteral("profile"), writingProfileName(override.profile)},
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

QString SettingsCodecs::openAiModel() const
{
    const QString model = value(SettingsKeys::OpenAiModel, QStringLiteral("gpt-5.6-luna")).toString().trimmed();
    return model.isEmpty() ? QStringLiteral("gpt-5.6-luna") : model;
}

void SettingsCodecs::setOpenAiModel(const QString &value)
{
    const QString model = value.trimmed();
    m_settings.setValue(SettingsKeys::OpenAiModel,
                        model.isEmpty() ? QStringLiteral("gpt-5.6-luna") : model);
}

QString SettingsCodecs::openAiAuthMode() const
{
    const QString mode = value(SettingsKeys::OpenAiAuthMode, QStringLiteral("auto")).toString();
    if (mode == QStringLiteral("codex_then_api_key")) {
        return QStringLiteral("auto");
    }
    if (mode == QStringLiteral("api_key_env")) {
        return QStringLiteral("env");
    }
    if (mode == QStringLiteral("api_key_settings")) {
        return QStringLiteral("settings");
    }
    if (mode == QStringLiteral("auto") || mode == QStringLiteral("codex_api_key") || mode == QStringLiteral("codex_oauth")
        || mode == QStringLiteral("env") || mode == QStringLiteral("settings") || mode == QStringLiteral("cliproxy")) {
        return mode;
    }
    return QStringLiteral("auto");
}

void SettingsCodecs::setOpenAiAuthMode(const QString &value)
{
    if (value == QStringLiteral("codex_api_key") || value == QStringLiteral("codex_oauth")
        || value == QStringLiteral("env") || value == QStringLiteral("settings") || value == QStringLiteral("cliproxy")) {
        m_settings.setValue(SettingsKeys::OpenAiAuthMode, value);
        return;
    }
    m_settings.setValue(SettingsKeys::OpenAiAuthMode, QStringLiteral("auto"));
}

QString SettingsCodecs::openAiEffort() const
{
    const QString effort = value(SettingsKeys::OpenAiEffort, QStringLiteral("none")).toString();
    if (effort == QStringLiteral("none") || effort == QStringLiteral("low") || effort == QStringLiteral("medium")
        || effort == QStringLiteral("high") || effort == QStringLiteral("xhigh")) {
        return effort;
    }
    return QStringLiteral("none");
}

void SettingsCodecs::setOpenAiEffort(const QString &value)
{
    if (value == QStringLiteral("none") || value == QStringLiteral("low") || value == QStringLiteral("medium")
        || value == QStringLiteral("high") || value == QStringLiteral("xhigh")) {
        m_settings.setValue(SettingsKeys::OpenAiEffort, value);
        return;
    }
    m_settings.setValue(SettingsKeys::OpenAiEffort, QStringLiteral("none"));
}

bool SettingsCodecs::openAiFastMode() const
{
    return value(SettingsKeys::OpenAiFastMode, true).toBool();
}

void SettingsCodecs::setOpenAiFastMode(bool value)
{
    m_settings.setValue(SettingsKeys::OpenAiFastMode, value);
}

QString SettingsCodecs::anthropicModel() const
{
    const QString model = value(SettingsKeys::AnthropicModel, QStringLiteral("claude-sonnet-4-6")).toString().trimmed();
    return model.isEmpty() ? QStringLiteral("claude-sonnet-4-6") : model;
}

void SettingsCodecs::setAnthropicModel(const QString &value)
{
    const QString model = value.trimmed();
    m_settings.setValue(SettingsKeys::AnthropicModel,
                        model.isEmpty() ? QStringLiteral("claude-sonnet-4-6") : model);
}

QString SettingsCodecs::anthropicAuthMode() const
{
    const QString mode = value(SettingsKeys::AnthropicAuthMode, QStringLiteral("oauth")).toString();
    return mode == QStringLiteral("cliproxy") ? mode : QStringLiteral("oauth");
}

void SettingsCodecs::setAnthropicAuthMode(const QString &value)
{
    m_settings.setValue(SettingsKeys::AnthropicAuthMode,
                        value == QStringLiteral("cliproxy") ? value : QStringLiteral("oauth"));
}

QString SettingsCodecs::anthropicEffort() const
{
    const QString effort = value(SettingsKeys::AnthropicEffort, QStringLiteral("low")).toString();
    if (effort == QStringLiteral("low") || effort == QStringLiteral("medium")
        || effort == QStringLiteral("high") || effort == QStringLiteral("xhigh")
        || effort == QStringLiteral("max")) {
        return effort;
    }
    return QStringLiteral("low");
}

void SettingsCodecs::setAnthropicEffort(const QString &value)
{
    if (value == QStringLiteral("low") || value == QStringLiteral("medium")
        || value == QStringLiteral("high") || value == QStringLiteral("xhigh")
        || value == QStringLiteral("max")) {
        m_settings.setValue(SettingsKeys::AnthropicEffort, value);
        return;
    }
    m_settings.setValue(SettingsKeys::AnthropicEffort, QStringLiteral("low"));
}

bool SettingsCodecs::anthropicFastMode() const
{
    return value(SettingsKeys::AnthropicFastMode, true).toBool();
}

void SettingsCodecs::setAnthropicFastMode(bool value)
{
    m_settings.setValue(SettingsKeys::AnthropicFastMode, value);
}

QString SettingsCodecs::outputMethod() const
{
    return OutputMethod::normalized(value(SettingsKeys::OutputMethod, QString::fromLatin1(OutputMethod::Automatic)).toString());
}

void SettingsCodecs::setOutputMethod(const QString &value)
{
    m_settings.setValue(SettingsKeys::OutputMethod, OutputMethod::normalized(value));
}

OutputFormat SettingsCodecs::outputFormat() const
{
    return outputFormatFromString(value(SettingsKeys::OutputFormat, QStringLiteral("plain")).toString());
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
    if (!value && outputMethod() == QString::fromLatin1(OutputMethod::Ydotool)) {
        setOutputMethod(QString::fromLatin1(OutputMethod::Automatic));
    }
}

bool SettingsCodecs::restoreClipboardAfterTyping() const
{
    return value(SettingsKeys::RestoreClipboardAfterTyping, false).toBool();
}

void SettingsCodecs::setRestoreClipboardAfterTyping(bool value)
{
    m_settings.setValue(SettingsKeys::RestoreClipboardAfterTyping, value);
}

int SettingsCodecs::completionStatusDurationMs() const
{
    return std::clamp(value(SettingsKeys::CompletionStatusDurationMs, 500).toInt(), 0, 5000);
}

void SettingsCodecs::setCompletionStatusDurationMs(int value)
{
    m_settings.setValue(SettingsKeys::CompletionStatusDurationMs, std::clamp(value, 0, 5000));
}

QList<PasteRule> SettingsCodecs::pasteRules() const
{
    const QByteArray encoded = value(SettingsKeys::PasteRules, QByteArray()).toByteArray();
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
    return rules.isEmpty() ? defaultPasteRules() : rules;
}

void SettingsCodecs::setPasteRules(const QList<PasteRule> &rules)
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
    m_settings.setValue(SettingsKeys::PasteRules, QJsonDocument(array).toJson(QJsonDocument::Compact));
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

QString SettingsCodecs::cliproxyBaseUrl() const
{
    QString base = value(SettingsKeys::CliproxyBaseUrl, QString()).toString().trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    return base;
}

void SettingsCodecs::setCliproxyBaseUrl(const QString &value)
{
    QString base = value.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    m_settings.setValue(SettingsKeys::CliproxyBaseUrl, base);
}

QString SettingsCodecs::cliproxyApiKey() const
{
    return value(SettingsKeys::CliproxyApiKey, QString()).toString().trimmed();
}

void SettingsCodecs::setCliproxyApiKey(const QString &value)
{
    m_settings.setValue(SettingsKeys::CliproxyApiKey, value.trimmed());
}

QString SettingsCodecs::claudeCredentialsPath() const
{
    return value(SettingsKeys::ClaudeCredentialsPath, QDir::homePath() + QStringLiteral("/.claude/.credentials.json")).toString();
}

QString SettingsCodecs::claudeEndpointBase() const
{
    return value(SettingsKeys::ClaudeEndpointBase, QStringLiteral("https://claude.ai")).toString();
}

QString SettingsCodecs::claudeVoicePath() const
{
    return value(SettingsKeys::ClaudeVoicePath, QStringLiteral("/api/ws/speech_to_text/voice_stream")).toString();
}

QString SettingsCodecs::storedApiKeyFallback() const
{
    return value(SettingsKeys::OpenAiApiKey, QString()).toString();
}

void SettingsCodecs::setStoredApiKeyFallback(const QString &value)
{
    m_settings.setValue(SettingsKeys::OpenAiApiKey, value);
}

void SettingsCodecs::clearStoredApiKeyFallback()
{
    m_settings.remove(SettingsKeys::OpenAiApiKey);
}

AppSettings SettingsCodecs::snapshot() const
{
    AppSettings settings;
    settings.setupCompleted = setupCompleted();
    settings.launchAtLogin = launchAtLogin();
    settings.ui.previewWords = previewWords();
    settings.ui.theme = theme();
    settings.ui.pauseMediaDuringTranscription = pauseMediaDuringTranscription();
    settings.ui.soundsEnabled = soundsEnabled();

    settings.speech.providerId = speechProvider();
    settings.speech.claudeAuthMode = anthropicAuthMode();
    settings.speech.codexAuthMode = openAiAuthMode();
    settings.speech.vocabulary = customVocabulary();
    settings.speech.claudeCredentialsPath = claudeCredentialsPath();
    settings.speech.claudeEndpointBase = claudeEndpointBase();
    settings.speech.claudeVoicePath = claudeVoicePath();
    settings.speech.cliproxyOauthDir = cliproxyOauthDir();
    settings.speech.claudeCliproxyAccount = anthropicCliproxyAccount();
    settings.speech.codexCliproxyAccount = openAiCliproxyAccount();
    settings.audio = audioCaptureSettings();
    settings.appRecognitionRules = appRecognitionRules();
    settings.bindings = bindingRules();
    settings.vocabulary = vocabularyEntries();
    settings.correctionLearningEnabled = correctionLearningEnabled();
    settings.learnedCorrections = learnedCorrections();
    for (const LearnedCorrection &correction : settings.learnedCorrections) {
        if (!correction.enabled) {
            continue;
        }
        if (!settings.speech.vocabulary.contains(correction.corrected, Qt::CaseInsensitive)) {
            settings.speech.vocabulary.append(correction.corrected);
        }
    }

    settings.refinement.providerId = refinementProvider();
    settings.refinement.style = refinementStyle();
    settings.refinement.openAiModel = openAiModel();
    settings.refinement.openAiAuthMode = openAiAuthMode();
    settings.refinement.openAiEffort = openAiEffort();
    settings.refinement.openAiFastMode = openAiFastMode();
    settings.refinement.openAiCliproxyAccount = openAiCliproxyAccount();
    settings.refinement.anthropicModel = anthropicModel();
    settings.refinement.anthropicAuthMode = anthropicAuthMode();
    settings.refinement.anthropicEffort = anthropicEffort();
    settings.refinement.anthropicFastMode = anthropicFastMode();
    settings.refinement.anthropicCliproxyAccount = anthropicCliproxyAccount();
    settings.refinement.cliproxyOauthDir = cliproxyOauthDir();
    settings.refinement.cliproxyBaseUrl = cliproxyBaseUrl();
    settings.refinement.cliproxyApiKey = cliproxyApiKey();
    settings.refinement.anthropicEndpointBase = QStringLiteral("https://api.anthropic.com/v1");
    settings.refinement.claudeCredentialsPath = claudeCredentialsPath();
    settings.refinement.defaultWritingProfile = defaultWritingProfile();
    settings.refinement.writingProfiles = writingProfileSettings();
    settings.refinement.writingProfileOverrides = writingProfileOverrides();
    settings.refinement.useTargetContext = useTargetContext();
    settings.refinement.includeScreenshotContext = includeScreenshotContext();

    settings.output.method = outputMethod();
    settings.output.format = outputFormat();
    settings.output.ydotoolEnabled = ydotoolEnabled();
    settings.output.restoreClipboardAfterTyping = restoreClipboardAfterTyping();
    settings.output.completionStatusDurationMs = completionStatusDurationMs();
    settings.output.pasteRules = pasteRules();
    settings.updates.channel = updateChannel();
    settings.updates.autoCheck = autoCheckUpdates();
    settings.updates.autoInstall = autoInstallUpdates();
    return settings;
}

} // namespace speecher
