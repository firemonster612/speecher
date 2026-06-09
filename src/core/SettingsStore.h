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

    QList<BindingRule> bindingRules() const;
    bool setBindingRules(const QList<BindingRule> &rules, QString *error = nullptr);

    QString refinementProvider() const;
    void setRefinementProvider(const QString &value);

    QString refinementStyle() const;
    void setRefinementStyle(const QString &value);

    QString openAiModel() const;
    void setOpenAiModel(const QString &value);

    QString openAiAuthMode() const;
    void setOpenAiAuthMode(const QString &value);

    QString outputMethod() const;
    void setOutputMethod(const QString &value);
    bool ydotoolEnabled() const;
    void setYdotoolEnabled(bool value);

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
