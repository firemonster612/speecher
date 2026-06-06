#pragma once

#include <QObject>
#include <QSettings>
#include <QStringList>

#include "core/AppSettings.h"

namespace speecher {

class SettingsStore : public QObject {
    Q_OBJECT

public:
    explicit SettingsStore(QObject *parent = nullptr);

    int previewWords() const;
    void setPreviewWords(int value);

    QString theme() const;
    void setTheme(const QString &value);

    bool pauseMediaDuringTranscription() const;
    void setPauseMediaDuringTranscription(bool value);

    QString speechProvider() const;
    void setSpeechProvider(const QString &value);

    QStringList customVocabulary() const;
    void setCustomVocabulary(const QStringList &value);

    QString refinementProvider() const;
    void setRefinementProvider(const QString &value);

    QString refinementStyle() const;
    void setRefinementStyle(const QString &value);

    QString refinementOutputFormat() const;
    void setRefinementOutputFormat(const QString &value);

    QString openAiModel() const;
    QString openAiAuthMode() const;
    void setOpenAiAuthMode(const QString &value);

    QString outputTypeCommand() const;
    bool fallbackClipboard() const;

    QString claudeCredentialsPath() const;
    QString claudeEndpointBase() const;
    QString claudeVoicePath() const;

    QString storedApiKeyFallback() const;
    void setStoredApiKeyFallback(const QString &value);
    void clearStoredApiKeyFallback();

    AppSettings snapshot() const;
    QSettings &raw();

private:
    QVariant value(const QString &key, const QVariant &fallback) const;
    QSettings m_settings;
};

} // namespace speecher
