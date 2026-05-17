#include "ui/TranscriberPopup.h"

#include "ui/WaveformWidget.h"
#include "ui/WaylandLayerShell.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QEvent>
#include <QFontMetrics>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace speecher {

TranscriberPopup::TranscriberPopup(QWidget *parent)
    : QWidget(parent)
    , m_previewPill(new QFrame(this))
    , m_preview(new QLabel(this))
    , m_waveform(new WaveformWidget(this))
    , m_layer(new WaylandLayerShell(this))
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    m_layer->configurePopup(this);
    setObjectName(QStringLiteral("transcriberPopup"));
    applyTheme();

    m_previewPill->setObjectName(QStringLiteral("previewPill"));
    m_previewPill->setFixedHeight(48);
    m_previewPill->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *previewLayout = new QHBoxLayout(m_previewPill);
    previewLayout->setContentsMargins(24, 0, 24, 0);
    previewLayout->setSpacing(0);

    m_preview->setWordWrap(false);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setText(QStringLiteral("---"));
    previewLayout->addWidget(m_preview);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(10);
    layout->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);

    layout->addWidget(m_waveform, 0, Qt::AlignHCenter);
    layout->addWidget(m_previewPill, 0, Qt::AlignHCenter);
}

QSize TranscriberPopup::sizeHint() const
{
    return QSize(620, 110);
}

void TranscriberPopup::setStatus(const QString &status)
{
    Q_UNUSED(status);
    adjustSize();
    updateWindowMask();
}

void TranscriberPopup::setPreview(const QString &preview)
{
    QString cleaned = preview.simplified();
    if (!cleaned.isEmpty()) {
        const QFontMetrics metrics(m_preview->font());
        const int maxTextWidth = 520;
        while (metrics.horizontalAdvance(cleaned) > maxTextWidth) {
            const int firstSpace = cleaned.indexOf(' ');
            if (firstSpace < 0) {
                cleaned.clear();
                break;
            }
            cleaned = cleaned.mid(firstSpace + 1).trimmed();
        }
    }
    m_preview->setText(cleaned.isEmpty() ? QStringLiteral("---") : cleaned);
    m_preview->setVisible(true);
    m_previewPill->setVisible(true);
    m_preview->setMaximumWidth(520);
    m_previewPill->resize(m_previewPill->sizeHint().width(), 48);
    updateWindowMask();
}

void TranscriberPopup::hidePreview()
{
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
    m_waveform->setMode(refining ? WaveformWidget::Mode::Dots : WaveformWidget::Mode::Waveform);
}

void TranscriberPopup::setFrozen(bool frozen)
{
    m_waveform->setMode(frozen ? WaveformWidget::Mode::Frozen : WaveformWidget::Mode::Waveform);
}

void TranscriberPopup::showMessage(const QString &message)
{
    m_waveform->setMessage(message);
    updateWindowMask();
}

void TranscriberPopup::showPopup()
{
    m_layer->positionBottomCenter(this);
    updateWindowMask();
    show();
    raise();
}

void TranscriberPopup::changeEvent(QEvent *event)
{
    if (!m_applyingTheme && (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)) {
        applyTheme();
    }
    QWidget::changeEvent(event);
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
    const QPalette p = palette();
    const bool dark = p.color(QPalette::Window).lightness() < 128;
    const QString pill = dark ? QStringLiteral("#1d1d1d") : QStringLiteral("#f2f0e6");
    const QString stroke = dark ? QStringLiteral("rgba(255,255,236,46)") : QStringLiteral("rgba(32,32,32,42)");
    const QString text = dark ? QStringLiteral("#ffffec") : QStringLiteral("#202020");
    setStyleSheet(QStringLiteral(
                      "#transcriberPopup{background:transparent;}"
                      "QFrame#previewPill{background:%1;border:1px solid %2;border-radius:24px;}"
                      "QLabel{color:%3;font:14px 'Inter','Noto Sans',sans-serif;}")
                      .arg(pill, stroke, text));
    m_applyingTheme = false;
}

void TranscriberPopup::updateWindowMask()
{
    clearMask();
}

} // namespace speecher
