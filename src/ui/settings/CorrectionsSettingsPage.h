#pragma once

#include "core/AppSettings.h"

#include <QFrame>

class QCheckBox;
class QPushButton;
class QTableWidget;

namespace speecher {

class CorrectionsSettingsPage : public QFrame {
    Q_OBJECT

public:
    explicit CorrectionsSettingsPage(QWidget *parent = nullptr);

    void load(bool learningEnabled, const QList<LearnedCorrection> &corrections);
    void appendToDraft(AppSettings &draft) const;
    bool learningEnabled() const;
    QList<LearnedCorrection> corrections() const;
    bool hasChanges(bool learningEnabled, const QList<LearnedCorrection> &corrections) const;
    void setTargetAccessibilityAvailable(bool available);

signals:
    void changed();

private:
    void setCorrections(const QList<LearnedCorrection> &corrections);

    QCheckBox *m_learnCorrections;
    QTableWidget *m_corrections;
    QPushButton *m_removeCorrectionButton;
    QPushButton *m_undoCorrectionButton;
    QPushButton *m_undoLatestLearnButton;
    QList<LearnedCorrection> m_removedCorrections;
};

} // namespace speecher
