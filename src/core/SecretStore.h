#pragma once

#include <QObject>
#include <QString>

namespace speecher {

class SettingsStore;

class SecretStore : public QObject {
    Q_OBJECT

public:
    explicit SecretStore(SettingsStore *settings, QObject *parent = nullptr);

    QString apiKey() const;
    bool saveApiKey(const QString &apiKey);
    QString status() const;
    QString lastError() const;
    bool usesInsecureSettingsFallback() const;

private:
    QString keyringApiKey() const;
    bool writeKeyringApiKey(const QString &apiKey) const;
    bool deleteKeyringApiKey() const;
    void migrateLegacySettingsKey();

    SettingsStore *m_settings;
    mutable QString m_lastError;
};

} // namespace speecher
