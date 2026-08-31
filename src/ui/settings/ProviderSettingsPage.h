#pragma once

#include <QScrollArea>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QStackedWidget;

namespace speecher {

struct AppSettings;
class SecretStore;
class SettingsStore;

class ProviderSettingsPage : public QScrollArea {
    Q_OBJECT

public:
    explicit ProviderSettingsPage(SettingsStore &settings, SecretStore &secrets, QWidget *parent = nullptr);

    // The models and auth halves embed into different sidebar sections.
    QWidget *modelsContent() const { return m_modelsContent; }
    QWidget *authContent() const { return m_authContent; }

    void loadModels();
    void loadAuthModes();
    void loadSecret();
    void appendToDraft(AppSettings &draft) const;
    void saveAuthModes();
    bool saveSecret();
    bool hasModelChanges() const;
    bool hasAuthChanges() const;

signals:
    void changed();

private:
    void updateAuthControl();
    void updateAnthropicControls();
    void updateAnthropicAuthControl();
    static QString comboSelection(const QComboBox *combo, const QString &stored);
    void populateCliproxyAccounts(QComboBox *combo, const QString &type, const QString &selected);
    QString editedCliproxyBaseUrl() const;
    void updateCliproxyServerVisibility();

    SettingsStore &m_settings;
    SecretStore &m_secrets;
    QComboBox *m_openAiModel;
    QComboBox *m_openAiEffort;
    QCheckBox *m_openAiFastMode;
    QComboBox *m_anthropicModel;
    QComboBox *m_anthropicEffort;
    QCheckBox *m_anthropicFastMode;
    QComboBox *m_authMode;
    QComboBox *m_anthropicAuthMode;
    QWidget *m_modelsContent = nullptr;
    QWidget *m_authContent = nullptr;
    QComboBox *m_openAiCliproxyAccount;
    QComboBox *m_anthropicCliproxyAccount;
    QLineEdit *m_cliproxyBaseUrl = nullptr;
    QLineEdit *m_cliproxyApiKey = nullptr;
    QWidget *m_cliproxyCard = nullptr;
    QStackedWidget *m_authControl;
    QLabel *m_authStatus;
    QLabel *m_anthropicWarning;
    QLabel *m_anthropicAuthStatus = nullptr;
    QWidget *m_anthropicWarningRow = nullptr;
    QLineEdit *m_apiKey;
    QString m_loadedApiKey;
    quint64 m_apiKeyEditRevision = 0;
    quint64 m_authStatusGeneration = 0;
    bool m_secretLoaded = false;
};

} // namespace speecher
