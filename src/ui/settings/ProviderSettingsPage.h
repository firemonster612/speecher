#pragma once

#include <QScrollArea>

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
    void showAnthropicAuthInfo();

    SettingsStore &m_settings;
    SecretStore &m_secrets;
    QComboBox *m_openAiModel;
    QComboBox *m_openAiEffort;
    QComboBox *m_anthropicModel;
    QComboBox *m_anthropicEffort;
    QComboBox *m_authMode;
    QComboBox *m_anthropicAuthMode;
    QComboBox *m_openAiCliproxyAccount;
    QComboBox *m_anthropicCliproxyAccount;
    QStackedWidget *m_authControl;
    QLabel *m_authStatus;
    QLabel *m_anthropicWarning;
    QWidget *m_anthropicWarningRow = nullptr;
    QLineEdit *m_apiKey;
    QString m_loadedApiKey;
    quint64 m_apiKeyEditRevision = 0;
    quint64 m_authStatusGeneration = 0;
    bool m_secretLoaded = false;
};

} // namespace speecher
