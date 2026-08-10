#pragma once

#include "core/AppSettings.h"

#include <QScrollArea>

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace speecher {

class GeneralSettingsPage : public QScrollArea {
    Q_OBJECT

public:
    explicit GeneralSettingsPage(const QString &primaryOutputStatus,
                                 QWidget *parent = nullptr);

    void load(const AppSettings &settings);
    void appendToDraft(AppSettings &draft) const;
    bool hasChanges(const AppSettings &settings) const;

signals:
    void changed();
    void setupRequested();

private:
    QComboBox *m_theme;
    QCheckBox *m_pauseMedia;
    QCheckBox *m_sounds;
    QSpinBox *m_previewWords;
};

} // namespace speecher
