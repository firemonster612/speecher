#include "ui/TranscriberPopup.h"

#include "app/UpdateBanner.h"
#include "dictation/DictationTypes.h"
#include "platform/FallbackPopupPositioner.h"
#include "ui/WaveformWidget.h"

#include <QApplication>
#include <QColor>
#include <QEasingCurve>
#include <QFrame>
#include <QEvent>
#include <QHBoxLayout>
#include <QFontMetrics>
#include <QPalette>
#include <QPaintEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QStyle>
#include <QPropertyAnimation>
#include <QTimer>
#include <QVBoxLayout>

#include <QPainter>
#include <QPainterPath>

#include <algorithm>

#ifdef Q_OS_MACOS
#include "platform/mac/MacWindowChrome.h"
#endif

namespace speecher {
namespace {

// Lifts the error countdown bar clear of the capsule's bottom border, which
// it otherwise sits on as a square-ended strip crossing the hairline.
constexpr int kErrorBarInset = 9;

// Half the 48px waveform capsule: the listening pill is a true stadium, and
// every taller capsule (transcript over the waveform strip, wrapped errors)
// keeps this same corner radius so it reads as the same pill grown taller,
// not a different widget with bulging semicircular ends.
constexpr qreal kPillCornerRadius = 24.0;

// One line of live transcript. Narrower than the error wrap on purpose:
// while speaking, the capsule should stay a compact pill rather than a
// screen-wide banner, and the tail-trimming keeps the newest words visible
// whatever the width.
constexpr int kMaxPreviewWidth = 440;

// Match the native popups' room around the text and lower strip.
constexpr QMargins kPreviewMargins{24, 12, 24, 8};
constexpr int kPreviewStripSpacing = 8;

// The preview contour: the empty corners beside the narrow waveform strip are
// carved away, leaving a wide bar around the text and a rounded lobe hugging
// the strip below, joined by concave fillets so the outline stays smoothly
// rounded everywhere.
constexpr qreal kContourFillet = 12.0;   // the concave turn from shoulder into lobe
constexpr qreal kLobePad = 10.0;         // lobe air either side of the strip's ink
// The shoulder sits as far below the text as the pill's top sits above it,
// so the wide bar reads evenly padded around the preview line.
constexpr qreal kShoulderDrop = kPreviewMargins.top();

// Paints the pill instead of a stylesheet border: Qt's QSS rounded borders
// render with uneven thickness at fractional display scales, which reads as
// blur around the edge. This draws a one-device-pixel hairline aligned to the
// device-pixel grid.
class PillFrame final : public QFrame {
public:
    using QFrame::QFrame;

    // Only the popup's preview pill sets these; banners and the error capsule
    // keep the plain outline. With both widgets visible the frame carves the
    // preview contour around them instead of a full rounded rectangle.
    void setContourWidgets(QWidget *text, WaveformWidget *strip)
    {
        m_text = text;
        m_strip = strip;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
#ifdef Q_OS_MACOS
        return;
#else
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
        QPainterPath path = contourPath(pillRect);
        if (path.isEmpty()) {
            const qreal radius = std::min(pillRect.height() / 2.0, kPillCornerRadius);
            path.addRoundedRect(pillRect, radius, radius);
        }
        painter.drawPath(path);
#endif
    }

private:
    // One connected outline, traced clockwise: a stadium-ended bar around the
    // text, concave fillets turning down beside the strip, and a rounded lobe
    // hugging it. Empty when there is no width difference worth carving, so
    // the caller falls back to the plain capsule.
    QPainterPath contourPath(const QRectF &pillRect) const
    {
        QPainterPath path;
        if (!m_text || !m_strip || !m_text->isVisible() || !m_strip->isVisible()) {
            return path;
        }
        const qreal shoulderY = m_text->y() + m_text->height() + kShoulderDrop;
        const qreal capR = (shoulderY - pillRect.top()) / 2.0;
        // The lobe hugs what the strip paints — the bar row or the
        // status text — not the strip's fixed widget bounds, which are much
        // wider than the ink they hold.
        const qreal stripCenter = m_strip->x() + m_strip->width() / 2.0;
        const qreal lobeHalf = m_strip->contentWidth() / 2.0 + kLobePad;
        const qreal lobeLeft = stripCenter - lobeHalf;
        const qreal lobeRight = stripCenter + lobeHalf;
        const qreal lobeHeight = pillRect.bottom() - shoulderY;
        // Radii yield to the room available: the fillet takes what the shelf
        // between end cap and lobe leaves it, and fillet plus bottom corner
        // shrink together to fit the lobe's height, so the outline can never
        // double back on itself. Under ~4px of fillet there is nothing left
        // to carve and the plain capsule is the honest shape.
        const qreal shelf = std::min(lobeLeft - pillRect.left(),
                                     pillRect.right() - lobeRight);
        qreal fillet = std::min(kContourFillet, shelf - capR);
        qreal lobeR = std::min(kPillCornerRadius, lobeHalf);
        if (fillet + lobeR > lobeHeight) {
            const qreal scale = lobeHeight / (fillet + lobeR);
            fillet *= scale;
            lobeR *= scale;
        }
        if (capR <= 0 || lobeHeight <= 0 || fillet < 4) {
            return path;
        }

        path.moveTo(pillRect.left() + capR, pillRect.top());
        path.lineTo(pillRect.right() - capR, pillRect.top());
        // Right stadium end of the text bar.
        path.arcTo(QRectF(pillRect.right() - 2 * capR, pillRect.top(),
                          2 * capR, shoulderY - pillRect.top()),
                   90, -180);
        path.lineTo(lobeRight + fillet, shoulderY);
        // Concave fillet into the lobe's right side.
        path.arcTo(QRectF(lobeRight, shoulderY, 2 * fillet, 2 * fillet), 90, 90);
        path.lineTo(lobeRight, pillRect.bottom() - lobeR);
        path.arcTo(QRectF(lobeRight - 2 * lobeR, pillRect.bottom() - 2 * lobeR,
                          2 * lobeR, 2 * lobeR),
                   0, -90);
        path.lineTo(lobeLeft + lobeR, pillRect.bottom());
        path.arcTo(QRectF(lobeLeft, pillRect.bottom() - 2 * lobeR, 2 * lobeR, 2 * lobeR),
                   270, -90);
        path.lineTo(lobeLeft, shoulderY + fillet);
        // Concave fillet back onto the shoulder.
        path.arcTo(QRectF(lobeLeft - 2 * fillet, shoulderY, 2 * fillet, 2 * fillet),
                   0, 90);
        path.lineTo(pillRect.left() + capR, shoulderY);
        // Left stadium end back up to the start.
        path.arcTo(QRectF(pillRect.left(), pillRect.top(),
                          2 * capR, shoulderY - pillRect.top()),
                   270, -180);
        path.closeSubpath();
        return path;
    }

    QWidget *m_text = nullptr;
    WaveformWidget *m_strip = nullptr;
};

// The popup's action chips: capsule buttons in the pill's own visual language,
// painted like PillFrame because the popup floats on a translucent window
// where a rectangular style-drawn button would not fit. Clickable chips fill
// with the Highlight role so they read as buttons at a glance; a disabled chip
// (a progress state such as "Downloading 42%") falls back to the pill's Base
// capsule and reads as status. Palette roles only.
class ChipButton final : public QPushButton {
public:
    explicit ChipButton(QWidget *parent = nullptr)
        : QPushButton(parent)
    {
        setFlat(true);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover);
    }

    QSize sizeHint() const override
    {
        const QSize label = fontMetrics().size(Qt::TextSingleLine, text());
        return QSize(label.width() + 2 * kHorizontalPadding,
                     label.height() + 2 * kVerticalPadding);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPalette p = palette();

        QColor fill = isEnabled() ? p.color(QPalette::Highlight)
                                  : p.color(QPalette::Base);
        if (isEnabled() && isDown()) {
            fill = fill.darker(115);
        } else if (isEnabled() && underMouse()) {
            fill = fill.lighter(110);
        }
        QColor stroke = p.color(QPalette::Mid);
        stroke.setAlpha(150);

        const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
        const qreal penWidth = 1.0 / dpr;
        const qreal inset = penWidth / 2.0;
        painter.setPen(isEnabled() ? Qt::NoPen : QPen(stroke, penWidth));
        painter.setBrush(fill);
        const QRectF capsule = QRectF(rect()).adjusted(inset, inset, -inset, -inset);
        painter.drawRoundedRect(capsule, capsule.height() / 2.0, capsule.height() / 2.0);

        painter.setPen(p.color(isEnabled() ? QPalette::HighlightedText
                                           : QPalette::PlaceholderText));
        painter.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    static constexpr int kHorizontalPadding = 14;
    static constexpr int kVerticalPadding = 6;
};

// The thin countdown under an error. Painted here rather than by the style: a
// 3px progress bar in any widget style still draws a frame, and the previous
// stylesheet that hid it hardcoded the colours it replaced.
class DismissBar final : public QProgressBar {
public:
    using QProgressBar::QProgressBar;

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (maximum() <= minimum()) {
            return;
        }
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(palette().color(QPalette::Highlight));
        const qreal fraction = qreal(value() - minimum()) / qreal(maximum() - minimum());
        QRectF chunk(rect());
        chunk.setWidth(chunk.width() * fraction);
        // Capsule ends, like every other shape on this popup.
        painter.drawRoundedRect(chunk, chunk.height() / 2.0, chunk.height() / 2.0);
    }
};

} // namespace

TranscriberPopup::TranscriberPopup(PopupPositioner *positioner, QWidget *parent)
    : QWidget(parent)
    , m_previewPill(new PillFrame(this))
    , m_preview(new QLabel(this))
    , m_errorDismissProgress(new DismissBar(m_previewPill))
    , m_waveform(new WaveformWidget(this))
    , m_positioner(positioner ? positioner : new FallbackPopupPositioner(this))
{
    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(2, 2, 2, 2);
    m_layout->setSpacing(10);
    m_layout->setAlignment(Qt::AlignHCenter | Qt::AlignBottom);

    if (m_positioner->parent() != this) {
        m_positioner->setParent(this);
    }
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAutoFillBackground(false);
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_ShowWithoutActivating);
    m_positioner->configurePopup(m_surface);
#ifdef Q_OS_MACOS
    mac::applyPopupChrome(this);
#endif
    setObjectName(QStringLiteral("transcriberPopup"));
    m_preview->setObjectName(QStringLiteral("rawTranscript"));
    // The pill paints a Base fill, so its text takes the Text role; the font
    // is whatever the desktop chose for the application.
    m_preview->setForegroundRole(QPalette::Text);
    applyTheme();

    m_previewPill->setObjectName(QStringLiteral("previewPill"));
    m_previewPill->setFrameShape(QFrame::NoFrame);
    m_previewPill->setAutoFillBackground(false);
    m_previewPill->setFixedHeight(m_waveform->height());
    m_previewPill->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // The waveform and preview share one capsule. Without words, only the
    // waveform occupies it. Its own background stays on for the Dictation page.
    m_waveform->setBackgroundVisible(false);
    m_preview->hide();
    m_pillLayout = new QVBoxLayout(m_previewPill);
    m_pillLayout->setContentsMargins(0, 0, 0, 0);
    m_pillLayout->setSpacing(0);

    m_preview->setWordWrap(false);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // The transcript line sits above a low waveform strip, both centred, so
    // the capsule keeps one symmetric silhouette: a plain waveform pill while
    // listening that grows upward once there are words.
    auto *previewRow = new QHBoxLayout;
    previewRow->setContentsMargins(0, 0, 0, 0);
    previewRow->setSpacing(10);
    // An error's warning sign, before its text as on the other platforms.
    m_errorIcon = new QLabel(m_previewPill);
    m_errorIcon->setObjectName(QStringLiteral("errorIcon"));
    const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    m_errorIcon->setPixmap(QIcon::fromTheme(QStringLiteral("dialog-warning"))
                               .pixmap(iconSize, iconSize));
    m_errorIcon->hide();
    previewRow->addWidget(m_errorIcon, 0, Qt::AlignVCenter);
    previewRow->addWidget(m_preview, 1);
    // An error's explicit way out, beside the auto-dismiss countdown, matching
    // the Dismiss buttons on the mac and Windows panels.
    m_errorDismiss = new ChipButton(m_previewPill);
    m_errorDismiss->setObjectName(QStringLiteral("errorDismiss"));
    m_errorDismiss->setText(popupDismissCaption());
    m_errorDismiss->hide();
    connect(m_errorDismiss, &QPushButton::clicked, this, [this] {
        m_errorDismissAnimation->stop();
        hide();
        emit errorDismissed();
    });
    previewRow->addWidget(m_errorDismiss, 0, Qt::AlignVCenter);
    m_pillLayout->addLayout(previewRow, 1);
    m_pillLayout->addWidget(m_waveform, 0, Qt::AlignHCenter);
    static_cast<PillFrame *>(m_previewPill)->setContourWidgets(m_preview, m_waveform);

    m_errorDismissProgress->setObjectName(QStringLiteral("errorDismissProgress"));
    m_errorDismissProgress->setRange(0, 1000);
    m_errorDismissProgress->setValue(m_errorDismissProgress->maximum());
    m_errorDismissProgress->setTextVisible(false);
    m_errorDismissProgress->setFixedHeight(3);
    m_errorDismissProgress->hide();
    m_pillLayout->addWidget(m_errorDismissProgress);

    m_errorDismissAnimation = new QPropertyAnimation(
        m_errorDismissProgress,
        QByteArrayLiteral("value"),
        this);
    m_errorDismissAnimation->setStartValue(m_errorDismissProgress->maximum());
    m_errorDismissAnimation->setEndValue(m_errorDismissProgress->minimum());
    m_errorDismissAnimation->setEasingCurve(QEasingCurve::Linear);
    connect(m_errorDismissAnimation, &QPropertyAnimation::finished, this, [this] {
        hide();
        emit errorDismissed();
    });

    // Both notices are banners in the pill's own capsule: a plain message with
    // an explicitly labelled button beside it, so the action reads as a button
    // rather than asking the user to guess that colored text is clickable.
    const auto makeBanner = [this](const char *name, QLabel *&text) {
        auto *banner = new PillFrame(this);
        banner->setObjectName(QLatin1String(name));
        banner->setFrameShape(QFrame::NoFrame);
        banner->setAutoFillBackground(false);
        auto *layout = new QHBoxLayout(banner);
        layout->setContentsMargins(16, 5, 5, 5);
        layout->setSpacing(10);
        text = new QLabel(banner);
        text->setForegroundRole(QPalette::Text);
        layout->addWidget(text);
        banner->hide();
        m_layout->addWidget(banner, 0, Qt::AlignHCenter);
        return banner;
    };

    m_whatsNewRow = makeBanner("whatsNewRow", m_whatsNewText);
    m_whatsNewText->setObjectName(QStringLiteral("whatsNewText"));
    m_whatsNewAction = new ChipButton(m_whatsNewRow);
    m_whatsNewAction->setObjectName(QStringLiteral("whatsNewAction"));
    m_whatsNewDismiss = new ChipButton(m_whatsNewRow);
    m_whatsNewDismiss->setObjectName(QStringLiteral("whatsNewDismiss"));
    m_whatsNewDismiss->setText(QStringLiteral("✕"));
    m_whatsNewRow->layout()->addWidget(m_whatsNewAction);
    m_whatsNewRow->layout()->addWidget(m_whatsNewDismiss);
    connect(m_whatsNewAction, &QPushButton::clicked, this, &TranscriberPopup::whatsNewRequested);
    connect(m_whatsNewDismiss, &QPushButton::clicked, this, [this] {
        setWhatsNewBanner({}, false);
        emit whatsNewDismissed();
    });
    m_whatsNewAutoHide = new QTimer(this);
    m_whatsNewAutoHide->setObjectName(QStringLiteral("whatsNewAutoHide"));
    m_whatsNewAutoHide->setSingleShot(true);
    m_whatsNewAutoHide->setInterval(6000);
    // Auto-hide only tidies this popup; the offer stays pending and returns
    // with the next popup, unlike the dismiss button.
    connect(m_whatsNewAutoHide, &QTimer::timeout, this, [this] {
        setWhatsNewBanner({}, false);
    });

    m_updateBanner = makeBanner("updateBanner", m_updateBannerText);
    m_updateBannerText->setObjectName(QStringLiteral("updateBannerText"));
    m_updateBannerAction = new ChipButton(m_updateBanner);
    m_updateBannerAction->setObjectName(QStringLiteral("updateBannerAction"));
    m_updateBanner->layout()->addWidget(m_updateBannerAction);
    connect(m_updateBannerAction, &QPushButton::clicked,
            this, &TranscriberPopup::updateRequested);
    // No settings prompts here: the overlay cannot take focus and shows while
    // the user is speaking. Desktop accessibility is offered on the Dictation
    // page and in the setup assistant.
    m_layout->addWidget(m_previewPill, 0, Qt::AlignHCenter);
}

QSize TranscriberPopup::sizeHint() const
{
    // configurePopup asks for the hint from the constructor, before the
    // banners exist.
    const int spacing = m_layout->spacing();
    const auto bannerHeight = [spacing](const QFrame *banner) {
        return !banner || banner->isHidden() ? 0
                                             : banner->sizeHint().height() + spacing;
    };
    const int pillHeight = m_pillLayout ? m_previewPill->height() : m_waveform->height();
    // Room for the widest a capsule gets, so an error's icon, wrapped text and
    // Dismiss chip are never clipped by the window.
    const int width = std::max(620, m_layout->sizeHint().width());
    return QSize(width, pillHeight + 4 + bannerHeight(m_updateBanner) + bannerHeight(m_whatsNewRow));
}

void TranscriberPopup::setStatus(const QString &status)
{
    // "Stopping" is the one state whose label drives this popup: the mic is
    // closed but the provider is still finalising, so the waveform gives way
    // to a shimmering "Transcribing…" and the stale speech preview goes away.
    if (status == QStringLiteral("Stopping")) {
        m_phase = Phase::Transcribing;
        restoreStandardLayout();
        hidePreview();
        m_waveform->setStatusText(dictationStatusLabel(QStringLiteral("stopping")));
        m_previewPill->adjustSize();
    }
    adjustSize();
}

void TranscriberPopup::setPreview(const QString &preview)
{
    if (m_phase != Phase::Live) {
        return;
    }
    restoreStandardLayout();
    m_waveform->setMode(WaveformWidget::Mode::Waveform);
    applyPreviewText(preview);
}

void TranscriberPopup::setRefinementPreview(const QString &preview)
{
    if (m_phase != Phase::Refining) {
        return;
    }
    applyPreviewText(preview);
}

void TranscriberPopup::applyPreviewText(const QString &preview)
{
    const QFontMetrics metrics(m_preview->font());
    const int maxTextWidth = kMaxPreviewWidth;
    const QString visible = trimPreviewToFit(preview, [&](const QString &candidate) {
        return metrics.horizontalAdvance(candidate) <= maxTextWidth;
    });
    if (visible.isEmpty()) {
        // Keep the waveform capsule when there are no words to preview.
        hidePreview();
        return;
    }
    m_preview->setText(visible);
    m_preview->setVisible(true);
    m_previewPill->setVisible(true);
    m_preview->setMaximumWidth(maxTextWidth);
    applyPillGeometry();
    adjustSize();
    // The capsule grows upward and shrinks back as words come and go; on
    // fallback positioning the window would otherwise keep its top edge and
    // push the taller capsule past the screen's bottom margin.
    repositionIfVisible();
}

void TranscriberPopup::repositionIfVisible()
{
    if (isVisible()) {
        m_positioner->positionBottomCenter(m_surface);
    }
}

void TranscriberPopup::hidePreview()
{
    m_preview->hide();
    applyPillGeometry();
    adjustSize();
    if (isVisible()) {
        m_positioner->positionBottomCenter(m_surface);
    }
}

// The capsule's two standard shapes: the bare waveform pill while there are
// no words, and the grown capsule holding the transcript line over the
// compact waveform strip. Errors size themselves in showErrorMessage.
void TranscriberPopup::applyPillGeometry()
{
    const bool hasWords = !m_preview->isHidden();
    m_waveform->setCompact(hasWords);
    m_pillLayout->setSpacing(hasWords ? kPreviewStripSpacing : 0);
    if (!hasWords) {
        m_pillLayout->setContentsMargins(0, 0, 0, 0);
        m_previewPill->setFixedHeight(m_waveform->height());
        return;
    }
    m_pillLayout->setContentsMargins(kPreviewMargins);
    m_previewPill->setFixedHeight(kPreviewMargins.top() + m_preview->sizeHint().height()
                                  + kPreviewStripSpacing
                                  + m_waveform->height() + kPreviewMargins.bottom());
}

void TranscriberPopup::setLevel(float level)
{
    m_waveform->setLevel(level);
}

void TranscriberPopup::setRefining(bool refining)
{
    if (refining) {
        m_phase = Phase::Refining;
        restoreStandardLayout();
        hidePreview();
        m_waveform->setStatusText(dictationStatusLabel(QStringLiteral("refining")));
        return;
    }
    m_phase = Phase::Live;
    m_waveform->setMode(WaveformWidget::Mode::Waveform);
}

void TranscriberPopup::setFrozen(bool frozen)
{
    if (frozen) {
        // Between "Transcribing…" and "Refining…" the session freezes the
        // popup; keep the shimmer rather than flashing a stilled waveform.
        if (m_phase == Phase::Live) {
            m_waveform->setMode(WaveformWidget::Mode::Frozen);
        }
        return;
    }
    m_phase = Phase::Live;
    m_waveform->setMode(WaveformWidget::Mode::Waveform);
}

void TranscriberPopup::showOAuthRefreshIndicator()
{
    m_phase = Phase::Live;
    restoreStandardLayout();
    hidePreview();
    m_waveform->setStatusText(renewingSignInText());
    m_previewPill->adjustSize();
    adjustSize();
}

void TranscriberPopup::showListeningIndicator()
{
    m_phase = Phase::Live;
    restoreStandardLayout();
    m_waveform->setMode(WaveformWidget::Mode::Waveform);
    adjustSize();
}

// A run with no break point, such as a long URL, gets zero-width spaces
// wherever it would pass the wrap width, so QLabel's word wrap can break it
// instead of the capsule growing past the width every platform shares.
static QString breakableRuns(const QString &text, const QFontMetrics &metrics, int width)
{
    QString result;
    for (const QString &word : text.split(QLatin1Char(' '))) {
        if (!result.isEmpty()) {
            result += QLatin1Char(' ');
        }
        QString line;
        for (const QChar c : word) {
            if (metrics.horizontalAdvance(line + c) > width) {
                result += line + QChar(0x200B);
                line.clear();
            }
            line += c;
        }
        result += line;
    }
    return result;
}

// The theme icon for each receipt: the document went out, or it sits on the
// clipboard. A fallback delivered text, but not the text asked for.
static QIcon outcomeIcon(PopupOutcome outcome)
{
    switch (outcome) {
    case PopupOutcome::Inserted:
        return QIcon::fromTheme(QStringLiteral("document-send"));
    case PopupOutcome::Copied:
        return QIcon::fromTheme(QStringLiteral("edit-copy"));
    case PopupOutcome::Fallback:
        return QIcon::fromTheme(QStringLiteral("dialog-information"));
    case PopupOutcome::Error:
        return QIcon::fromTheme(QStringLiteral("dialog-warning"));
    }
    return {};
}

void TranscriberPopup::showMessage(const QString &message, PopupOutcome outcome)
{
    m_phase = Phase::Live;
    // The receipt replaces the waveform and any last preview words.
    restoreStandardLayout();
    hidePreview();
    m_waveform->setMessage(message, outcomeIcon(outcome));
    m_previewPill->adjustSize();
    adjustSize();
}

void TranscriberPopup::showErrorMessage(const QString &message)
{
    m_phase = Phase::Live;
    m_errorDismissAnimation->stop();
    m_waveform->hide();
    const QFontMetrics metrics(m_preview->font());
    constexpr int maxTextWidth = kPopupErrorWrapWidth;
    const QString text = breakableRuns(message.simplified(), metrics, maxTextWidth);
    // The capsule hugs a short error instead of stretching to the full wrap
    // width around one small centred line. The wrapped layout's own width,
    // since a rounded advance can be a fraction short and break the line.
    constexpr int wrapFlags = Qt::TextWordWrap;
    const int textWidth = qBound(1,
                                 metrics.boundingRect(QRect(0, 0, maxTextWidth, 1000),
                                                      wrapFlags, text)
                                     .width(),
                                 maxTextWidth);
    m_preview->setText(text);
    m_preview->setWordWrap(true);
    m_preview->setFixedWidth(textWidth);
    m_preview->setVisible(true);
    m_errorIcon->setVisible(true);
    m_errorDismiss->setVisible(true);
    m_previewPill->setVisible(true);
    // previewRow is centred in what is left after the bar and its air, so the
    // same amount above it puts the text on the capsule's optical centre.
    m_pillLayout->setSpacing(0);
    m_pillLayout->setContentsMargins(24, kErrorBarInset + 3, 24, kErrorBarInset);
    m_errorDismissProgress->setValue(m_errorDismissProgress->maximum());
    m_errorDismissProgress->show();

    const int textHeight = metrics.boundingRect(
                                      QRect(0, 0, textWidth, 1000),
                                      Qt::AlignCenter | wrapFlags,
                                      text)
                               .height();
    // 24 keeps the label's 12px above and below the text; 3 is the countdown
    // bar; the inset is the air between the bar and the border.
    m_previewPill->setFixedHeight(
        qMax(48, textHeight + 24 + 2 * (3 + kErrorBarInset)));
    m_previewPill->resize(m_previewPill->sizeHint());
    adjustSize();
    if (isVisible()) {
        m_positioner->positionBottomCenter(m_surface);
    }
    m_errorDismissAnimation->setDuration(popupErrorDismissMs(message));
    m_errorDismissAnimation->start();
}

void TranscriberPopup::showPopup(quint64 generation)
{
    // A previous dictation's error belongs to the attempt that failed: its
    // text, its Dismiss chip, its taller pill and its draining countdown all
    // go before this dictation is shown. Without this the countdown could
    // hide a live dictation's popup and report a dismissal against it.
    restoreStandardLayout();
    hidePreview();
    m_pendingPresentationGeneration = generation;
    m_positioner->positionBottomCenter(m_surface);
    show();
    raise();
    update();
    if (!m_whatsNewRow->isHidden()) {
        m_whatsNewAutoHide->start();
    }
}

void TranscriberPopup::setWhatsNewBanner(const WhatsNewBannerModel &banner, bool visible)
{
    const bool visibilityChanged = m_whatsNewRow->isHidden() == visible;
    m_whatsNewText->setText(banner.text);
    m_whatsNewAction->setText(banner.action);
    m_whatsNewDismiss->setToolTip(banner.dismiss);
    m_whatsNewDismiss->setAccessibleName(banner.dismiss);
    m_whatsNewRow->setVisible(visible);
    if (visible && isVisible()) {
        m_whatsNewAutoHide->start();
    } else if (!visible) {
        m_whatsNewAutoHide->stop();
    }
    if (!visibilityChanged) {
        return;
    }
    adjustSize();
    if (isVisible()) {
        m_positioner->positionBottomCenter(m_surface);
    }
}

void TranscriberPopup::setUpdateBanner(const UpdateBannerModel &banner)
{
    const bool visibilityChanged = m_updateBanner->isHidden() == banner.visible;
    const bool textChanged = m_updateBannerText->text() != banner.text
        || m_updateBannerAction->text() != banner.action;
    m_updateBannerText->setText(banner.text);
    m_updateBannerAction->setText(banner.action);
    m_updateBannerAction->setVisible(!banner.action.isEmpty());
    m_updateBannerAction->setEnabled(banner.actionEnabled);
    m_updateBanner->setVisible(banner.visible);
    if (!visibilityChanged && !textChanged) {
        return;
    }
    adjustSize();
    repositionIfVisible();
}

void TranscriberPopup::changeEvent(QEvent *event)
{
    if (!m_applyingTheme && (event->type() == QEvent::PaletteChange || event->type() == QEvent::ApplicationPaletteChange)) {
        applyTheme();
    }
    QWidget::changeEvent(event);
}

void TranscriberPopup::hideEvent(QHideEvent *event)
{
    // Whatever hid the popup, a countdown left running would hide the next
    // dictation's popup when it finished.
    m_errorDismissAnimation->stop();
    QWidget::hideEvent(event);
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

void TranscriberPopup::applyTheme()
{
    if (m_applyingTheme) {
        return;
    }
    m_applyingTheme = true;
    // Colours come from palette roles set once in the constructor; a palette
    // change only needs the painted pill and bar redrawn.
    if (m_previewPill) {
        m_previewPill->update();
    }
    if (m_errorDismissProgress) {
        m_errorDismissProgress->update();
    }
    m_applyingTheme = false;
}

void TranscriberPopup::restoreStandardLayout()
{
    m_errorDismissAnimation->stop();
    m_errorDismissProgress->hide();
    m_errorDismiss->hide();
    m_errorIcon->hide();
    m_waveform->show();
    m_preview->setWordWrap(false);
    m_preview->setMinimumWidth(0);
    m_preview->setMaximumWidth(kMaxPreviewWidth);
    applyPillGeometry();
}

} // namespace speecher
