#pragma once

#include "ui/AppPage.h"

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
    QPushButton *toggleButton() const;

public slots:
    void setStatus(const QString &status);
    void refreshSummary();

signals:
    void navigateRequested(AppPageId page);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void setSummaryText(QLabel *label, const QString &text);

    ApplicationController *m_controller;
    AccessibilityNotice *m_accessibilityNotice;
    QPushButton *m_toggle;
    QLabel *m_status;
    WaveformWidget *m_waveform;
    QLabel *m_provider;
    QLabel *m_microphone;
    QLabel *m_output;
    QLabel *m_theme;
};

} // namespace speecher
