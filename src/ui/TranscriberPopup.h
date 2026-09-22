#pragma once

#include "frontend/qt/QtPopupSurface.h"

#include <QLabel>
#include <QWidget>

class QFrame;
class QEvent;
class QHideEvent;
class QProgressBar;
class QPushButton;
class QPropertyAnimation;
class QPaintEvent;
class QTimer;
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
    void setRefinementPreview(const QString &preview);
    void hidePreview();
    void setLevel(float level);
    void setRefining(bool refining);
    void setFrozen(bool frozen);
    void showOAuthRefreshIndicator();
    void showListeningIndicator();
    void showMessage(const QString &message);
    void showErrorMessage(const QString &message);
    void showPopup(quint64 generation);
    // A banner is a capsule holding a plain message and, when there is
    // something to do, an explicitly labelled button ("Install and restart").
    // An empty message hides the banner; an empty action hides the button.
    void setUpdateBanner(const QString &message, const QString &action, bool actionEnabled);
    void setWhatsNewBanner(const QString &message, bool visible);

signals:
    void errorDismissed();
    void popupPresented(quint64 generation);
    void updateRequested();
    void whatsNewRequested();
    void whatsNewDismissed();

protected:
    void changeEvent(QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    // After the mic stops the popup walks Transcribing then Refining; the live
    // speech preview is suppressed for both so only refinement text streams in.
    enum class Phase { Live, Transcribing, Refining };

    void applyTheme();
    void applyPreviewText(const QString &preview);
    void applyPillGeometry();
    void restoreStandardLayout();

    QVBoxLayout *m_layout = nullptr;
    QFrame *m_previewPill = nullptr;
    QVBoxLayout *m_pillLayout = nullptr;
    QLabel *m_preview = nullptr;
    QPushButton *m_errorDismiss = nullptr;
    QProgressBar *m_errorDismissProgress = nullptr;
    QPropertyAnimation *m_errorDismissAnimation = nullptr;
    WaveformWidget *m_waveform = nullptr;
    QFrame *m_updateBanner = nullptr;
    QLabel *m_updateBannerText = nullptr;
    QPushButton *m_updateBannerAction = nullptr;
    QFrame *m_whatsNewRow = nullptr;
    QLabel *m_whatsNewText = nullptr;
    QPushButton *m_whatsNewAction = nullptr;
    QPushButton *m_whatsNewDismiss = nullptr;
    QTimer *m_whatsNewAutoHide = nullptr;
    PopupPositioner *m_positioner = nullptr;
    QtPopupSurface m_surface{this};
    quint64 m_pendingPresentationGeneration = 0;
    bool m_applyingTheme = false;
    Phase m_phase = Phase::Live;
};

} // namespace speecher
