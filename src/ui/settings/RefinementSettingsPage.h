#pragma once

#include "core/AppSettings.h"

#include <QScrollArea>

class QCheckBox;
class QComboBox;
class QFrame;
class QTableWidget;

namespace speecher {

class ProviderRegistry;

class RefinementSettingsPage : public QScrollArea {
    Q_OBJECT

public:
    explicit RefinementSettingsPage(ProviderRegistry &providers, QWidget *parent = nullptr);

    void load(const AppSettings &settings);
    void appendToDraft(AppSettings &draft) const;
    bool hasChanges(const AppSettings &settings) const;
    void setTargetAccessibilityAvailable(bool available);

signals:
    void changed();

private:
    QList<WritingProfileSettings> currentWritingProfileSettings() const;
    void setWritingProfileSettings(const QList<WritingProfileSettings> &settings);
    void updateScreenshotControl();

    QComboBox *m_provider;
    QComboBox *m_writingProfile;
    QCheckBox *m_useTargetContext;
    QCheckBox *m_screenshotContext;
    QTableWidget *m_profileSettings;
    QFrame *m_targetContextControl = nullptr;
};

} // namespace speecher
