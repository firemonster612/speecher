#pragma once

#include "frontend/qt/SchemaSettingsPage.h"

class QComboBox;
class QLabel;
class QLineEdit;

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

private:
    SchemaCustomRow makeAuthModeRow(QWidget *parent, std::function<void()> notifyChanged);
    SchemaCustomRow makeCredentialRow(QWidget *parent, std::function<void()> notifyChanged);
    SchemaCustomRow makeAnthropicAuthModeRow(QWidget *parent, std::function<void()> notifyChanged);
    SchemaCustomRow makeCliproxyBaseUrlRow(QWidget *parent,
                                           std::function<void()> notifyChanged);
    SchemaCustomRow makeCliproxyApiKeyRow(QWidget *parent,
                                         std::function<void()> notifyChanged);
    // The picker for one provider's CLI Proxy API account, which only speaks
    // while `mode` says the credentials come from CLI Proxy API.
    SchemaCustomRow makeCliproxyAccountRow(QComboBox *account,
                                           const QComboBox *mode,
                                           const QString &type,
                                           QString *stored,
                                           std::function<void()> notifyChanged);
    void populateCliproxyAccounts(QComboBox *account, const QString &type, const QString &selected);
    static QString comboSelection(const QComboBox *account, const QString &stored);
    QString editedCliproxyBaseUrl() const;
    QString editedCliproxyApiKey() const;
    void updateAccountTooltips();
    void updateCredentialControl();
    void updateAnthropicAuthControl();

    SettingsStore &m_settings;
    SecretStore &m_secrets;
    QComboBox *m_authMode = nullptr;
    QComboBox *m_anthropicAuthMode = nullptr;
    QComboBox *m_openAiCliproxyAccount = nullptr;
    QComboBox *m_anthropicCliproxyAccount = nullptr;
    QLineEdit *m_cliproxyBaseUrl = nullptr;
    QLineEdit *m_cliproxyApiKey = nullptr;
    QLabel *m_authStatus = nullptr;
    QLabel *m_anthropicAuthStatus = nullptr;
    QLineEdit *m_apiKey = nullptr;
    QString m_openAiStoredAccount;
    QString m_anthropicStoredAccount;
    QString m_loadedApiKey;
    // A keyring read that lands after typing started must not overwrite it.
    quint64 m_apiKeyEditRevision = 0;
    quint64 m_authStatusGeneration = 0;
    bool m_secretLoaded = false;
};

} // namespace speecher
