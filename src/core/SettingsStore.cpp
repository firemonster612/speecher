#include "core/SettingsStore.h"

#include "core/OutputMethod.h"
#include "core/VocabularyLimit.h"

#include <QDir>

namespace speecher {

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

QString SettingsStore::refinementOutputFormat() const
{
    const QString format = value(QStringLiteral("refinement/outputFormat"), QStringLiteral("plain_sentences")).toString();
    if (format == QStringLiteral("markdown")) {
        return format;
    }
    return QStringLiteral("plain_sentences");
}

void SettingsStore::setRefinementOutputFormat(const QString &value)
{
    m_settings.setValue(QStringLiteral("refinement/outputFormat"), value == QStringLiteral("markdown") ? value : QStringLiteral("plain_sentences"));
}

QString SettingsStore::openAiModel() const
{
    return value(QStringLiteral("openai/model"), QStringLiteral("gpt-5.4-mini")).toString();
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

QString SettingsStore::outputTypeCommand() const
{
    return value(QStringLiteral("output/typeCommand"), QStringLiteral("wtype")).toString();
}

QString SettingsStore::outputMethod() const
{
    return OutputMethod::normalized(value(QStringLiteral("output/method"), QString::fromLatin1(OutputMethod::Automatic)).toString());
}

void SettingsStore::setOutputMethod(const QString &value)
{
    m_settings.setValue(QStringLiteral("output/method"), OutputMethod::normalized(value));
}

bool SettingsStore::fallbackClipboard() const
{
    return value(QStringLiteral("output/fallbackClipboard"), true).toBool();
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

    settings.refinement.providerId = refinementProvider();
    settings.refinement.style = refinementStyle();
    settings.refinement.outputFormat = refinementOutputFormat();
    settings.refinement.openAiModel = openAiModel();
    settings.refinement.openAiAuthMode = openAiAuthMode();

    settings.output.method = outputMethod();
    settings.output.typeCommand = outputTypeCommand();
    settings.output.fallbackClipboard = fallbackClipboard();
    settings.output.ydotoolEnabled = ydotoolEnabled();
    return settings;
}

QSettings &SettingsStore::raw()
{
    return m_settings;
}

} // namespace speecher
