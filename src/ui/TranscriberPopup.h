#pragma once

#include "frontend/qt/QtPopupSurface.h"

#include <QLabel>
#include <QWidget>

class QFrame;
class QEvent;
class QProgressBar;
class QPushButton;
class QPropertyAnimation;
class QPaintEvent;
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
    void setUpdateChip(const QString &text, bool visible, bool enabled);

signals:
    void errorDismissed();
    void popupPresented(quint64 generation);
    void updateRequested();

protected:
    void changeEvent(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    // After the mic stops the popup walks Transcribing then Refining; the live
    // speech preview is suppressed for both so only refinement text streams in.
    enum class Phase { Live, Transcribing, Refining };

    void applyTheme();
    void applyPreviewText(const QString &preview);
    void restoreStandardLayout();
    void setRefreshLayout(bool refreshLayout);
    void updateWindowMask();

    QVBoxLayout *m_layout = nullptr;
    QFrame *m_previewPill = nullptr;
    QLabel *m_preview = nullptr;
    QProgressBar *m_errorDismissProgress = nullptr;
    QPropertyAnimation *m_errorDismissAnimation = nullptr;
    WaveformWidget *m_waveform = nullptr;
    QPushButton *m_updateChip = nullptr;
    PopupPositioner *m_positioner = nullptr;
    QtPopupSurface m_surface{this};
    quint64 m_pendingPresentationGeneration = 0;
    bool m_applyingTheme = false;
    Phase m_phase = Phase::Live;
};

} // namespace speecher
