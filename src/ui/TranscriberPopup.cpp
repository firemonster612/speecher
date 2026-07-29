#include "ui/TranscriberPopup.h"

#include "platform/PlatformIntegration.h"
#include "ui/WaveformWidget.h"

#include <QApplication>
#include <QColor>
#include <QFrame>
#include <QHBoxLayout>
#include <QEvent>
#include <QPainter>
#include <QPalette>
#include <QResizeEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace speecher {
namespace {

QString rgbaString(QColor color, int alpha)
{
    color.setAlpha(alpha);
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

} // namespace

TranscriberPopup::TranscriberPopup(PopupPositioner *positioner, QWidget *parent)
    : QWidget(parent)
    , m_previewPill(new QFrame(this))
    , m_preview(new QLabel(this))
    , m_waveform(new WaveformWidget(this))
    , m_positioner(positioner ? positioner : PlatformFactory::create()->createPopupPositioner(this))
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
    m_previewPill->setMinimumHeight(48);
    m_previewPill->setMaximumHeight(92);
    m_previewPill->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *previewLayout = new QHBoxLayout(m_previewPill);
    previewLayout->setContentsMargins(24, 10, 24, 10);
    previewLayout->setSpacing(0);

    m_preview->setWordWrap(true);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setText(QStringLiteral("---"));
    previewLayout->addWidget(m_preview);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(2, 2, 2, 2);
    m_layout->setSpacing(10);
    m_layout->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);

    m_layout->addWidget(m_waveform, 0, Qt::AlignHCenter);
    m_layout->addWidget(m_previewPill, 0, Qt::AlignHCenter);
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
    setRefreshLayout(false);
    m_waveform->setMode(WaveformWidget::Mode::Waveform);
    const QString raw = preview.trimmed();
    QString visible = raw.simplified();
    constexpr qsizetype visibleCharacterLimit = 420;
    if (visible.size() > visibleCharacterLimit) {
        visible = QStringLiteral("… ") + visible.right(visibleCharacterLimit);
    }
    m_preview->setText(visible.isEmpty() ? QStringLiteral("---") : visible);
    m_preview->setToolTip(raw);
    m_preview->setVisible(true);
    m_previewPill->setVisible(true);
    m_preview->setFixedWidth(540);
    m_previewPill->resize(588, qMin(m_previewPill->sizeHint().height(), 92));
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

void TranscriberPopup::showPopup()
{
    m_positioner->positionBottomCenter(this);
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
    const QPalette p = qApp ? qApp->palette() : palette();
    const QColor pill = p.color(QPalette::Base);
    const QString stroke = rgbaString(p.color(QPalette::Mid), 150);
    const QColor text = p.color(QPalette::Text);
    setStyleSheet(QStringLiteral(
                      "#transcriberPopup{background:transparent;}"
                      "QFrame#previewPill{background:%1;border:1px solid %2;border-radius:24px;}"
                      "QLabel{color:%3;font:14px 'Inter','Noto Sans',sans-serif;}")
                      .arg(pill.name(QColor::HexRgb), stroke, text.name(QColor::HexRgb)));
    m_applyingTheme = false;
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
