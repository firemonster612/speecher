#pragma once

#include <QLabel>
#include <QWidget>

class QFrame;
class QEvent;
class QResizeEvent;

namespace speecher {

class WaveformWidget;
class WaylandLayerShell;

class TranscriberPopup : public QWidget {
    Q_OBJECT

public:
    explicit TranscriberPopup(QWidget *parent = nullptr);
    QSize sizeHint() const override;

public slots:
    void setStatus(const QString &status);
    void setPreview(const QString &preview);
    void hidePreview();
    void setLevel(float level);
    void setRefining(bool refining);
    void setFrozen(bool frozen);
    void showMessage(const QString &message);
    void showPopup();

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyTheme();
    void updateWindowMask();

    QFrame *m_previewPill = nullptr;
    QLabel *m_preview = nullptr;
    WaveformWidget *m_waveform = nullptr;
    WaylandLayerShell *m_layer = nullptr;
    bool m_applyingTheme = false;
};

} // namespace speecher
