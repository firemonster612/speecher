#pragma once

#include "ui/AppPage.h"

#include <QScrollArea>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QToolButton;

namespace speecher {

class AccessibilityNotice;
class ApplicationController;
class LinuxGlobalShortcutSetupPage;
class WaveformWidget;

// The first page: the Global Shortcut editor where the desktop offers one,
// the dictation status with its transcript, and a summary of the settings
// that shape a session.
class DictationPage : public QScrollArea {
    Q_OBJECT

public:
    explicit DictationPage(ApplicationController *controller, QWidget *parent = nullptr);
    QPushButton *toggleButton() const;

public slots:
    void setStatus(const QString &status);
    void refreshSummary();

signals:
    void navigateRequested(AppPageId page);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyState(const QString &stateName);
    void setDisplayStatus(const QString &status);
    void updateSummary(bool resolveMicrophone);
    void updateShortcutSummary();
    void setSummaryText(QLabel *label, const QString &text);
    void applyToggleState(QPushButton *button, bool active, bool refining, const QString &state) const;

    ApplicationController *m_controller;
    LinuxGlobalShortcutSetupPage *m_shortcutEditor = nullptr;
    AccessibilityNotice *m_accessibilityNotice;
    QPushButton *m_heroToggle;
    QLabel *m_status;
    QLabel *m_errorText = nullptr;
    WaveformWidget *m_waveform;
    QPlainTextEdit *m_transcript;
    QToolButton *m_copyTranscript = nullptr;
    QLabel *m_provider;
    QLabel *m_microphone;
    QLabel *m_output;
    QLabel *m_shortcut = nullptr;
    bool m_sessionActive = false;
};

} // namespace speecher
