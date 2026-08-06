#pragma once

#include "core/AppSettings.h"

#include <QScrollArea>

class QCheckBox;
class QComboBox;
class QFrame;
class QPushButton;
class QTableWidget;

namespace speecher {

class ProviderRegistry;

class RefinementSettingsPage : public QScrollArea {
    Q_OBJECT

public:
    explicit RefinementSettingsPage(ProviderRegistry &providers, QWidget *parent = nullptr);

    void load(const AppSettings &settings);
    bool validate() const;
    void appendToDraft(AppSettings &draft) const;
    bool hasChanges(const AppSettings &settings) const;
    void setTargetAccessibilityAvailable(bool available);

signals:
    void changed();

private:
    QList<WritingProfileSettings> currentWritingProfileSettings() const;
    QList<WritingProfileOverride> currentWritingProfileOverrides() const;
    void setWritingProfileSettings(const QList<WritingProfileSettings> &settings);
    void setWritingProfileOverrides(const QList<WritingProfileOverride> &overrides);
    void addWritingProfileOverride(const WritingProfileOverride &override = {});
    void updateScreenshotControl();

    QComboBox *m_provider;
    QComboBox *m_writingProfile;
    QCheckBox *m_useTargetContext;
    QCheckBox *m_screenshotContext;
    QTableWidget *m_profileSettings;
    QTableWidget *m_appProfileOverrides;
    QPushButton *m_addAppProfileOverrideButton;
    QPushButton *m_removeAppProfileOverrideButton;
    QWidget *m_profileOverridesControl = nullptr;
    QFrame *m_targetContextControl = nullptr;
};

} // namespace speecher
