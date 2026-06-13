#include "core/SettingsStore.h"

#include "core/BindingProcessor.h"
#include "core/OutputMethod.h"
#include "core/VocabularyLimit.h"

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

} // namespace

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
    , m_settings(QStringLiteral("local.speecher"), QStringLiteral("speecher"))
{
}

QVariant SettingsStore::value(const QString &key, const QVariant &fallback) const
{
    return m_settings.value(key, fallback);
}

int SettingsStore::previewWords() const
{
    return std::clamp(value(QStringLiteral("ui/previewWords"), 8).toInt(), 1, 40);
}

void SettingsStore::setPreviewWords(int value)
{
    m_settings.setValue(QStringLiteral("ui/previewWords"), std::clamp(value, 1, 40));
}

QString SettingsStore::theme() const
{
    const QString theme = value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString();
    if (theme == QStringLiteral("light") || theme == QStringLiteral("dark")) {
        return theme;
    }
    return QStringLiteral("system");
}

void SettingsStore::setTheme(const QString &value)
{
    if (value == QStringLiteral("light") || value == QStringLiteral("dark")) {
        m_settings.setValue(QStringLiteral("ui/theme"), value);
        return;
    }
    m_settings.setValue(QStringLiteral("ui/theme"), QStringLiteral("system"));
}

bool SettingsStore::pauseMediaDuringTranscription() const
{
    return value(QStringLiteral("ui/pauseMediaDuringTranscription"), true).toBool();
}

void SettingsStore::setPauseMediaDuringTranscription(bool value)
{
    m_settings.setValue(QStringLiteral("ui/pauseMediaDuringTranscription"), value);
}

QString SettingsStore::speechProvider() const
{
    const QString provider = value(QStringLiteral("stt/provider"), QStringLiteral("claude")).toString();
    return provider.isEmpty() ? QStringLiteral("claude") : provider;
}

void SettingsStore::setSpeechProvider(const QString &value)
{
    m_settings.setValue(QStringLiteral("stt/provider"), value.isEmpty() ? QStringLiteral("claude") : value);
}

QStringList SettingsStore::customVocabulary() const
{
    return VocabularyLimit::limited(value(QStringLiteral("stt/customVocabulary"), QStringList()).toStringList());
}

void SettingsStore::setCustomVocabulary(const QStringList &value)
{
    m_settings.setValue(QStringLiteral("stt/customVocabulary"), VocabularyLimit::limited(value));
}

QString SettingsStore::audioInputDeviceId() const
{
    return audioCaptureSettings().deviceId;
}

void SettingsStore::setAudioInputDeviceId(const QString &value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.deviceId = value;
    setAudioCaptureSettings(settings);
}

QString SettingsStore::audioCaptureMode() const
{
    return audioCaptureSettings().mode;
}

void SettingsStore::setAudioCaptureMode(const QString &value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.mode = value;
    setAudioCaptureSettings(settings);
}

bool SettingsStore::audioVadEnabled() const
{
    return audioCaptureSettings().vadEnabled;
}

void SettingsStore::setAudioVadEnabled(bool value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.vadEnabled = value;
    setAudioCaptureSettings(settings);
}

int SettingsStore::audioPreRollMs() const
{
    return audioCaptureSettings().preRollMs;
}

void SettingsStore::setAudioPreRollMs(int value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.preRollMs = value;
    setAudioCaptureSettings(settings);
}

int SettingsStore::audioPostRollMs() const
{
    return audioCaptureSettings().postRollMs;
}

void SettingsStore::setAudioPostRollMs(int value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.postRollMs = value;
    setAudioCaptureSettings(settings);
}

int SettingsStore::audioReadinessTimeoutMs() const
{
    return audioCaptureSettings().readinessTimeoutMs;
}

void SettingsStore::setAudioReadinessTimeoutMs(int value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.readinessTimeoutMs = value;
    setAudioCaptureSettings(settings);
}

int SettingsStore::audioVadThresholdPercent() const
{
    return audioCaptureSettings().vadThresholdPercent;
}

void SettingsStore::setAudioVadThresholdPercent(int value)
{
    AudioCaptureSettings settings = audioCaptureSettings();
    settings.vadThresholdPercent = value;
    setAudioCaptureSettings(settings);
}

AudioCaptureSettings SettingsStore::audioCaptureSettings() const
{
    return normalizedAudioCaptureSettings({
        value(QStringLiteral("audio/deviceId"), QString()).toString(),
        value(QStringLiteral("audio/captureMode"), QStringLiteral("on_demand")).toString(),
        value(QStringLiteral("audio/vadEnabled"), false).toBool(),
        value(QStringLiteral("audio/preRollMs"), 250).toInt(),
        value(QStringLiteral("audio/postRollMs"), 200).toInt(),
        value(QStringLiteral("audio/readinessTimeoutMs"), 900).toInt(),
        value(QStringLiteral("audio/vadThresholdPercent"), 2).toInt(),
    });
}

void SettingsStore::setAudioCaptureSettings(const AudioCaptureSettings &value)
{
    const AudioCaptureSettings previous = audioCaptureSettings();
    const AudioCaptureSettings settings = normalizedAudioCaptureSettings(value);
    m_settings.setValue(QStringLiteral("audio/deviceId"), settings.deviceId);
    m_settings.setValue(QStringLiteral("audio/captureMode"), settings.mode);
    m_settings.setValue(QStringLiteral("audio/vadEnabled"), settings.vadEnabled);
    m_settings.setValue(QStringLiteral("audio/preRollMs"), settings.preRollMs);
    m_settings.setValue(QStringLiteral("audio/postRollMs"), settings.postRollMs);
    m_settings.setValue(QStringLiteral("audio/readinessTimeoutMs"), settings.readinessTimeoutMs);
    m_settings.setValue(QStringLiteral("audio/vadThresholdPercent"), settings.vadThresholdPercent);
    emitAudioCaptureSettingsChangedIfNeeded(previous);
}

QList<BindingRule> SettingsStore::bindingRules() const
{
    const QString stored = value(QStringLiteral("bindings/rules"), QString()).toString();
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

bool SettingsStore::setBindingRules(const QList<BindingRule> &rules, QString *error)
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
    m_settings.setValue(QStringLiteral("bindings/rules"),
                        QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
    return true;
}

QString SettingsStore::refinementProvider() const
{
    const QString provider = value(QStringLiteral("refinement/provider"), QStringLiteral("openai")).toString();
    return provider == QStringLiteral("none") ? provider : QStringLiteral("openai");
}

void SettingsStore::setRefinementProvider(const QString &value)
{
    m_settings.setValue(QStringLiteral("refinement/provider"), value == QStringLiteral("none") ? value : QStringLiteral("openai"));
}

QString SettingsStore::refinementStyle() const
{
    const QString style = value(QStringLiteral("refinement/style"), QStringLiteral("balanced")).toString();
    if (style == QStringLiteral("strong_polish") || style == QStringLiteral("balanced") || style == QStringLiteral("light_cleanup")) {
        return style;
    }
    return QStringLiteral("balanced");
}

void SettingsStore::setRefinementStyle(const QString &value)
{
    if (value == QStringLiteral("strong_polish") || value == QStringLiteral("light_cleanup")) {
        m_settings.setValue(QStringLiteral("refinement/style"), value);
        return;
    }
    m_settings.setValue(QStringLiteral("refinement/style"), QStringLiteral("balanced"));
}

QString SettingsStore::openAiModel() const
{
    const QString model = value(QStringLiteral("openai/model"), QStringLiteral("gpt-5.4-mini")).toString().trimmed();
    return model.isEmpty() ? QStringLiteral("gpt-5.4-mini") : model;
}

void SettingsStore::setOpenAiModel(const QString &value)
{
    const QString model = value.trimmed();
    m_settings.setValue(QStringLiteral("openai/model"),
                        model.isEmpty() ? QStringLiteral("gpt-5.4-mini") : model);
}

QString SettingsStore::openAiAuthMode() const
{
    const QString mode = value(QStringLiteral("openai/auth/mode"), QStringLiteral("auto")).toString();
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
        || mode == QStringLiteral("env") || mode == QStringLiteral("settings")) {
        return mode;
    }
    return QStringLiteral("auto");
}

void SettingsStore::setOpenAiAuthMode(const QString &value)
{
    if (value == QStringLiteral("codex_api_key") || value == QStringLiteral("codex_oauth")
        || value == QStringLiteral("env") || value == QStringLiteral("settings")) {
        m_settings.setValue(QStringLiteral("openai/auth/mode"), value);
        return;
    }
    m_settings.setValue(QStringLiteral("openai/auth/mode"), QStringLiteral("auto"));
}

QString SettingsStore::outputMethod() const
{
    return OutputMethod::normalized(value(QStringLiteral("output/method"), QString::fromLatin1(OutputMethod::Automatic)).toString());
}

void SettingsStore::setOutputMethod(const QString &value)
{
    m_settings.setValue(QStringLiteral("output/method"), OutputMethod::normalized(value));
}

bool SettingsStore::ydotoolEnabled() const
{
    return value(QStringLiteral("output/ydotoolEnabled"), false).toBool();
}

void SettingsStore::setYdotoolEnabled(bool value)
{
    m_settings.setValue(QStringLiteral("output/ydotoolEnabled"), value);
    if (!value && outputMethod() == QString::fromLatin1(OutputMethod::Ydotool)) {
        setOutputMethod(QString::fromLatin1(OutputMethod::Automatic));
    }
}

bool SettingsStore::restoreClipboardAfterTyping() const
{
    return value(QStringLiteral("output/restoreClipboardAfterTyping"), false).toBool();
}

void SettingsStore::setRestoreClipboardAfterTyping(bool value)
{
    m_settings.setValue(QStringLiteral("output/restoreClipboardAfterTyping"), value);
}

QString SettingsStore::claudeCredentialsPath() const
{
    return value(QStringLiteral("claude/credentialsPath"), QDir::homePath() + QStringLiteral("/.claude/.credentials.json")).toString();
}

QString SettingsStore::claudeEndpointBase() const
{
    return value(QStringLiteral("claude/endpointBase"), QStringLiteral("https://claude.ai")).toString();
}

QString SettingsStore::claudeVoicePath() const
{
    return value(QStringLiteral("claude/voicePath"), QStringLiteral("/api/ws/speech_to_text/voice_stream")).toString();
}

QString SettingsStore::storedApiKeyFallback() const
{
    return value(QStringLiteral("openai/apiKey"), QString()).toString();
}

void SettingsStore::setStoredApiKeyFallback(const QString &value)
{
    m_settings.setValue(QStringLiteral("openai/apiKey"), value);
}

void SettingsStore::clearStoredApiKeyFallback()
{
    m_settings.remove(QStringLiteral("openai/apiKey"));
}

AppSettings SettingsStore::snapshot() const
{
    AppSettings settings;
    settings.ui.previewWords = previewWords();
    settings.ui.theme = theme();
    settings.ui.pauseMediaDuringTranscription = pauseMediaDuringTranscription();

    settings.speech.providerId = speechProvider();
    settings.speech.vocabulary = customVocabulary();
    settings.speech.claudeCredentialsPath = claudeCredentialsPath();
    settings.speech.claudeEndpointBase = claudeEndpointBase();
    settings.speech.claudeVoicePath = claudeVoicePath();
    settings.audio = audioCaptureSettings();
    settings.bindings = bindingRules();

    settings.refinement.providerId = refinementProvider();
    settings.refinement.style = refinementStyle();
    settings.refinement.openAiModel = openAiModel();
    settings.refinement.openAiAuthMode = openAiAuthMode();

    settings.output.method = outputMethod();
    settings.output.ydotoolEnabled = ydotoolEnabled();
    settings.output.restoreClipboardAfterTyping = restoreClipboardAfterTyping();
    return settings;
}

QSettings &SettingsStore::raw()
{
    return m_settings;
}

void SettingsStore::emitAudioCaptureSettingsChangedIfNeeded(const AudioCaptureSettings &previous)
{
    const AudioCaptureSettings current = audioCaptureSettings();
    if (current != previous) {
        emit audioCaptureSettingsChanged(current);
    }
}

} // namespace speecher
