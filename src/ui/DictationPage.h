#pragma once

#include <QWidget>

class QLabel;
class QPushButton;

namespace speecher {

class AccessibilityNotice;
class ApplicationController;
class WaveformWidget;

class DictationPage : public QWidget {
    Q_OBJECT

public:
    explicit DictationPage(ApplicationController *controller, QWidget *parent = nullptr);

public slots:
    void setStatus(const QString &status);
    void refreshSummary();

signals:
    void navigateRequested(int settingsPage);

private:
    ApplicationController *m_controller;
    AccessibilityNotice *m_accessibilityNotice;
    QPushButton *m_toggle;
    QLabel *m_status;
    WaveformWidget *m_waveform;
    QLabel *m_provider;
    QLabel *m_output;
    QLabel *m_theme;
};

} // namespace speecher
