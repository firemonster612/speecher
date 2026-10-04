#pragma once

#include "frontend/qt/QtPopupSurface.h"
#include "dictation/DictationTypes.h"
#include "dictation/PopupPresentation.h"

#include <QLabel>
#include <QWidget>

class QFrame;
class QEvent;
class QGraphicsOpacityEffect;
class QHBoxLayout;
class QHideEvent;
class QProgressBar;
class QPushButton;
class QPropertyAnimation;
class QPaintEvent;
class QTimer;
class QToolButton;
class QVBoxLayout;

namespace speecher {

struct UpdateBannerModel;
struct WhatsNewBannerModel;
class PopupPositioner;
class WaveformWidget;

class TranscriberPopup : public QWidget {
    Q_OBJECT

public:
    explicit TranscriberPopup(PopupPositioner *positioner = nullptr, QWidget *parent = nullptr);
    QSize sizeHint() const override;

public slots:
    void setSessionState(DictationState state);
    void setPreview(const QString &preview);
    void setRefinementPreview(const QString &preview);
    void hidePreview();
    void setLevel(float level);
    void setRefining(bool refining);
    void setFrozen(bool frozen);
    void showOAuthRefreshIndicator();
    void showListeningIndicator();
    void showMessage(const QString &message, PopupOutcome outcome);
    // actionLabel names the one fix the error offers; empty for none.
    void showErrorMessage(const QString &message, const QString &actionLabel = QString());
    void showPopup(quint64 generation);
    // A banner is a capsule holding a plain message and, when there is
    // something to do, an explicitly labelled button ("Install and restart").
    void setUpdateBanner(const UpdateBannerModel &banner);
    void setWhatsNewBanner(const WhatsNewBannerModel &banner, bool visible);

signals:
    void errorDismissed();
    void errorActionRequested();
    void popupPresented(quint64 generation);
    void updateRequested();
    void updateLaterRequested();
    void updateDismissRequested();
    void whatsNewRequested();
    void whatsNewDismissed();
    // The buttons beside the waveform.
    void pauseToggled();
    void cancelRequested();

protected:
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    // After the mic stops the popup walks Transcribing then Refining; the live
    // speech preview is suppressed for both so only refinement text streams in.
    enum class Phase { Live, Transcribing, Refining };

    void applyTheme();
    void applyPreviewText(const QString &preview);
    void applyPillGeometry();
    void repositionIfVisible();
    void restoreStandardLayout();
    void applySessionControls();
    void applyFonts();
    void updatePreviewFade();
    // An error holds the capsule; its Dismiss chip shows exactly then.
    bool errorShown() const;

    QVBoxLayout *m_layout = nullptr;
    QFrame *m_previewPill = nullptr;
    QVBoxLayout *m_pillLayout = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_errorIcon = nullptr;
    QPushButton *m_errorDismiss = nullptr;
    QPushButton *m_errorAction = nullptr;
    QProgressBar *m_errorDismissProgress = nullptr;
    QPropertyAnimation *m_errorDismissAnimation = nullptr;
    WaveformWidget *m_waveform = nullptr;
    QToolButton *m_pauseButton = nullptr;
    QToolButton *m_cancelButton = nullptr;
    QWidget *m_busy = nullptr;
    QHBoxLayout *m_waveformRow = nullptr;
    QGraphicsOpacityEffect *m_previewFade = nullptr;
    // The preview lost words from its front, so its start fades.
    bool m_previewCut = false;
    DictationState m_sessionState = DictationState::Idle;
    QFrame *m_updateBanner = nullptr;
    QLabel *m_updateBannerText = nullptr;
    QLabel *m_updateBannerIcon = nullptr;
    QPushButton *m_updateBannerAction = nullptr;
    QPushButton *m_updateBannerLater = nullptr;
    QPushButton *m_updateBannerDismiss = nullptr;
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
