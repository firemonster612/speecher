#pragma once

#include <QLabel>
#include <QWidget>

class QFrame;
class QEvent;
class QProgressBar;
class QPropertyAnimation;
class QResizeEvent;
class QVBoxLayout;

namespace speecher {

class PopupPositioner;
class WaveformWidget;

class TranscriberPopup : public QWidget {
    Q_OBJECT

public:
    explicit TranscriberPopup(PopupPositioner *positioner = nullptr, QWidget *parent = nullptr);
    QSize sizeHint() const override;

public slots:
    void setStatus(const QString &status);
    void setPreview(const QString &preview);
    void hidePreview();
    void setLevel(float level);
    void setRefining(bool refining);
    void setFrozen(bool frozen);
    void showOAuthRefreshIndicator();
    void showListeningIndicator();
    void showMessage(const QString &message);
    void showErrorMessage(const QString &message);
    void showPopup();

signals:
    void errorDismissed();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyTheme();
    void restoreStandardLayout();
    void setRefreshLayout(bool refreshLayout);
    void updateWindowMask();

    QVBoxLayout *m_layout = nullptr;
    QFrame *m_previewPill = nullptr;
    QLabel *m_preview = nullptr;
    QProgressBar *m_errorDismissProgress = nullptr;
    QPropertyAnimation *m_errorDismissAnimation = nullptr;
    WaveformWidget *m_waveform = nullptr;
    PopupPositioner *m_positioner = nullptr;
    bool m_applyingTheme = false;
};

} // namespace speecher
