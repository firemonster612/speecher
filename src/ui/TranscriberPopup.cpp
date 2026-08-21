#include "ui/TranscriberPopup.h"

#include "platform/WaylandLayerShell.h"
#include "ui/AccessibilityNotice.h"
#include "ui/WaveformWidget.h"

#include <QApplication>
#include <QColor>
#include <QEasingCurve>
#include <QFrame>
#include <QEvent>
#include <QFontMetrics>
#include <QPalette>
#include <QPaintEvent>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>

#include <QPainter>

#include <algorithm>

namespace speecher {
namespace {

// Paints the pill instead of a stylesheet border: Qt's QSS rounded borders
// render with uneven thickness at fractional display scales, which reads as
// blur around the edge. This draws a one-device-pixel hairline aligned to the
// device-pixel grid.
class PillFrame final : public QFrame {
public:
    using QFrame::QFrame;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPalette p = QApplication::palette();
        QColor stroke = p.color(QPalette::Mid);
        stroke.setAlpha(150);
        const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
        const qreal penWidth = 1.0 / dpr;
        const qreal inset = penWidth / 2.0;
        painter.setPen(QPen(stroke, penWidth));
        painter.setBrush(p.color(QPalette::Base));
        const QRectF pillRect = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
        painter.drawRoundedRect(pillRect, pillRect.height() / 2.0, pillRect.height() / 2.0);
    }
};

} // namespace

TranscriberPopup::TranscriberPopup(PopupPositioner *positioner, QWidget *parent)
    : QWidget(parent)
    , m_previewPill(new PillFrame(this))
    , m_preview(new QLabel(this))
    , m_errorDismissProgress(new QProgressBar(m_previewPill))
    , m_waveform(new WaveformWidget(this))
    , m_accessibilityNotice(new AccessibilityNotice(this))
    , m_positioner(positioner ? positioner : new WaylandLayerShell(this))
{
    if (m_positioner->parent() != this) {
        m_positioner->setParent(this);
    }
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    m_positioner->configurePopup(this);
    setObjectName(QStringLiteral("transcriberPopup"));
    m_preview->setObjectName(QStringLiteral("rawTranscript"));
    applyTheme();

    m_previewPill->setObjectName(QStringLiteral("previewPill"));
    m_previewPill->setFixedHeight(48);
    m_previewPill->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *previewLayout = new QVBoxLayout(m_previewPill);
    previewLayout->setContentsMargins(24, 0, 24, 0);
    previewLayout->setSpacing(0);

    m_preview->setWordWrap(false);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setText(QStringLiteral("---"));
    previewLayout->addWidget(m_preview, 1);

    m_errorDismissProgress->setObjectName(QStringLiteral("errorDismissProgress"));
    m_errorDismissProgress->setRange(0, 1000);
    m_errorDismissProgress->setValue(m_errorDismissProgress->maximum());
    m_errorDismissProgress->setTextVisible(false);
    m_errorDismissProgress->setFixedHeight(3);
    m_errorDismissProgress->hide();
    previewLayout->addWidget(m_errorDismissProgress);

    m_errorDismissAnimation = new QPropertyAnimation(
        m_errorDismissProgress,
        QByteArrayLiteral("value"),
        this);
    m_errorDismissAnimation->setDuration(5000);
    m_errorDismissAnimation->setStartValue(m_errorDismissProgress->maximum());
    m_errorDismissAnimation->setEndValue(m_errorDismissProgress->minimum());
    m_errorDismissAnimation->setEasingCurve(QEasingCurve::Linear);
    connect(m_errorDismissAnimation, &QPropertyAnimation::finished, this, [this] {
        hide();
        emit errorDismissed();
    });

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(2, 2, 2, 2);
    m_layout->setSpacing(10);
    m_layout->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);

    m_accessibilityNotice->setCompact(true);
    m_accessibilityNotice->setMaximumWidth(620);
    connect(m_accessibilityNotice,
            &AccessibilityNotice::enableRequested,
            this,
            &TranscriberPopup::enableAccessibilityRequested);
    m_layout->addWidget(m_accessibilityNotice, 0, Qt::AlignHCenter);
    m_layout->addWidget(m_waveform, 0, Qt::AlignHCenter);
    m_layout->addWidget(m_previewPill, 0, Qt::AlignHCenter);
}

QSize TranscriberPopup::sizeHint() const
{
    const int noticeHeight = m_accessibilityNotice->isHidden()
        ? 0
        : m_accessibilityNotice->sizeHint().height() + m_layout->spacing();
    return QSize(620, 110 + noticeHeight);
}

void TranscriberPopup::setStatus(const QString &status)
{
    Q_UNUSED(status);
    adjustSize();
    updateWindowMask();
}

void TranscriberPopup::setPreview(const QString &preview)
{
    restoreStandardLayout();
    setRefreshLayout(false);
    m_waveform->setMode(WaveformWidget::Mode::Waveform);
    QString visible = preview.simplified();
    if (!visible.isEmpty()) {
        const QFontMetrics metrics(m_preview->font());
        constexpr int maxTextWidth = 520;
        while (metrics.horizontalAdvance(visible) > maxTextWidth) {
            const int firstSpace = visible.indexOf(QLatin1Char(' '));
            if (firstSpace < 0) {
                visible.clear();
                break;
            }
            visible = visible.mid(firstSpace + 1).trimmed();
        }
    }
    m_preview->setText(visible.isEmpty() ? QStringLiteral("---") : visible);
    m_preview->setVisible(true);
    m_previewPill->setVisible(true);
    m_preview->setMaximumWidth(520);
    m_previewPill->resize(m_previewPill->sizeHint().width(), 48);
    adjustSize();
    updateWindowMask();
}

void TranscriberPopup::hidePreview()
{
    setRefreshLayout(false);
    m_previewPill->hide();
    m_preview->hide();
    adjustSize();
    updateWindowMask();
}

void TranscriberPopup::setLevel(float level)
{
    m_waveform->setLevel(level);
}

void TranscriberPopup::setRefining(bool refining)
{
    setRefreshLayout(false);
    m_waveform->setMode(refining ? WaveformWidget::Mode::Dots : WaveformWidget::Mode::Waveform);
}

void TranscriberPopup::setFrozen(bool frozen)
{
    setRefreshLayout(false);
    m_waveform->setMode(frozen ? WaveformWidget::Mode::Frozen : WaveformWidget::Mode::Waveform);
}

void TranscriberPopup::showOAuthRefreshIndicator()
{
    restoreStandardLayout();
    setRefreshLayout(true);
    m_preview->setText(QStringLiteral("Refreshing OAuth token"));
    m_preview->setVisible(true);
    m_previewPill->setVisible(true);
    m_preview->setMaximumWidth(520);
    m_previewPill->resize(m_previewPill->sizeHint().width(), 48);
    m_waveform->setMode(WaveformWidget::Mode::Dots);
    adjustSize();
    updateWindowMask();
}

void TranscriberPopup::showListeningIndicator()
{
    restoreStandardLayout();
    setRefreshLayout(false);
    m_waveform->setMode(WaveformWidget::Mode::Waveform);
    adjustSize();
    updateWindowMask();
}

void TranscriberPopup::showMessage(const QString &message)
{
    setRefreshLayout(false);
    m_waveform->setMessage(message);
    updateWindowMask();
}

void TranscriberPopup::showErrorMessage(const QString &message)
{
    setRefreshLayout(false);
    m_errorDismissAnimation->stop();
    m_waveform->hide();
    m_preview->setText(message.simplified());
    m_preview->setWordWrap(true);
    m_preview->setFixedWidth(520);
    m_preview->setVisible(true);
    m_previewPill->setVisible(true);
    m_errorDismissProgress->setValue(m_errorDismissProgress->maximum());
    m_errorDismissProgress->show();

    const QFontMetrics metrics(m_preview->font());
    const int textHeight = metrics.boundingRect(
                                      QRect(0, 0, m_preview->width(), 1000),
                                      Qt::AlignCenter | Qt::TextWordWrap,
                                      m_preview->text())
                               .height();
    m_previewPill->setFixedHeight(qMax(48, textHeight + 27));
    m_previewPill->resize(m_previewPill->sizeHint());
    adjustSize();
    updateWindowMask();
    m_errorDismissAnimation->start();
}

void TranscriberPopup::showPopup(quint64 generation)
{
    m_pendingPresentationGeneration = generation;
    m_positioner->positionBottomCenter(this);
    updateWindowMask();
    show();
    raise();
    update();
}

void TranscriberPopup::setAccessibilityState(bool supported,
                                             bool enabled,
                                             bool persistent)
{
    m_accessibilityNotice->setState(supported, enabled, persistent);
    adjustSize();
    if (isVisible()) {
        m_positioner->positionBottomCenter(this);
    }
}

void TranscriberPopup::showAccessibilityError(const QString &message)
{
    m_accessibilityNotice->showError(message);
    adjustSize();
    if (isVisible()) {
        m_positioner->positionBottomCenter(this);
    }
}

void TranscriberPopup::changeEvent(QEvent *event)
{
    if (!m_applyingTheme && (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)) {
        applyTheme();
    }
    QWidget::changeEvent(event);
}

void TranscriberPopup::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (m_pendingPresentationGeneration == 0) {
        return;
    }
    const quint64 generation = m_pendingPresentationGeneration;
    m_pendingPresentationGeneration = 0;
    QTimer::singleShot(0, this, [this, generation] {
        emit popupPresented(generation);
    });
}

void TranscriberPopup::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateWindowMask();
}

void TranscriberPopup::applyTheme()
{
    if (m_applyingTheme) {
        return;
    }
    m_applyingTheme = true;
    const QPalette p = qApp ? qApp->palette() : palette();
    const QColor text = p.color(QPalette::Text);
    const QColor accent = p.color(QPalette::Highlight);
    setStyleSheet(QStringLiteral(
                      "#transcriberPopup{background:transparent;}"
                      "QFrame#previewPill{background:transparent;border:0;}"
                      "QLabel{color:%1;font:14px 'Inter','Noto Sans',sans-serif;}"
                      "QProgressBar#errorDismissProgress{border:0;background:transparent;}"
                      "QProgressBar#errorDismissProgress::chunk{background:%2;border-radius:1px;}")
                      .arg(text.name(QColor::HexRgb),
                           accent.name(QColor::HexRgb)));
    if (m_previewPill) {
        m_previewPill->update();
    }
    m_applyingTheme = false;
}

void TranscriberPopup::restoreStandardLayout()
{
    m_errorDismissAnimation->stop();
    m_errorDismissProgress->hide();
    m_waveform->show();
    m_preview->setWordWrap(false);
    m_preview->setMinimumWidth(0);
    m_preview->setMaximumWidth(520);
    m_previewPill->setFixedHeight(48);
}

void TranscriberPopup::setRefreshLayout(bool refreshLayout)
{
    if (!m_layout) {
        return;
    }

    const int previewIndex = m_layout->indexOf(m_previewPill);
    const int waveformIndex = m_layout->indexOf(m_waveform);
    const bool isRefreshLayout = previewIndex >= 0 && waveformIndex >= 0 && previewIndex < waveformIndex;
    if (isRefreshLayout == refreshLayout) {
        return;
    }

    m_layout->removeWidget(m_previewPill);
    m_layout->removeWidget(m_waveform);
    if (refreshLayout) {
        m_layout->addWidget(m_previewPill, 0, Qt::AlignHCenter);
        m_layout->addWidget(m_waveform, 0, Qt::AlignHCenter);
    } else {
        m_layout->addWidget(m_waveform, 0, Qt::AlignHCenter);
        m_layout->addWidget(m_previewPill, 0, Qt::AlignHCenter);
    }
}

void TranscriberPopup::updateWindowMask()
{
    clearMask();
}

} // namespace speecher
