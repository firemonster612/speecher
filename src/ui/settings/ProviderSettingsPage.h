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
    void loadAuth();
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
    void showAnthropicAuthInfo();

    SettingsStore &m_settings;
    SecretStore &m_secrets;
    QComboBox *m_openAiModel;
    QComboBox *m_openAiEffort;
    QComboBox *m_anthropicModel;
    QComboBox *m_anthropicEffort;
    QComboBox *m_authMode;
    QComboBox *m_anthropicAuthMode;
    QStackedWidget *m_authControl;
    QLabel *m_authStatus;
    QLabel *m_anthropicWarning;
    QWidget *m_anthropicWarningRow = nullptr;
    QLineEdit *m_apiKey;
};

} // namespace speecher
