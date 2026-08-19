#pragma once

#include "frontend/qt/SchemaSettingsPage.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QStackedWidget;

namespace speecher {

class SecretStore;
class SettingsStore;

// The three rows on the Providers page no descriptor can carry a value for:
// which credential source to use, and the credential itself, which lives in the
// keyring rather than in AppSettings.
class ProviderCustomRows {
public:
    ProviderCustomRows(SettingsStore &settings, SecretStore &secrets);

    SchemaCustomRowFactory factory();
    // The keyring read, once the window has painted.
    void loadSecret();
    // Says why itself when the keyring refuses, because only it knows.
    bool saveSecret();
    bool hasSecretChanges() const;

private:
    SchemaCustomRow makeAuthModeRow(QWidget *parent, std::function<void()> notifyChanged);
    SchemaCustomRow makeCredentialRow(QWidget *parent, std::function<void()> notifyChanged);
    SchemaCustomRow makeAnthropicAuthModeRow(QWidget *parent, std::function<void()> notifyChanged);
    void updateCredentialControl();

    SettingsStore &m_settings;
    SecretStore &m_secrets;
    QComboBox *m_authMode = nullptr;
    QStackedWidget *m_credential = nullptr;
    QLabel *m_authStatus = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QString m_loadedApiKey;
    // A keyring read that lands after typing started must not overwrite it.
    quint64 m_apiKeyEditRevision = 0;
    bool m_secretLoaded = false;
};

} // namespace speecher
