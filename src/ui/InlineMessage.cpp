#include "ui/InlineMessage.h"

#include "ui/settings/SettingsPageSupport.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QToolButton>

#ifdef SPEECHER_WITH_KCOLORSCHEME
#include <KColorScheme>
#endif

namespace speecher {

using settings::cornerRadius;
using settings::largeSpacing;
using settings::smallSpacing;

namespace {

// Kirigami.Units.iconSizes.smallMedium.
constexpr int kIconSize = 22;

QIcon typeIcon(InlineMessage::Type type, const QStyle *style)
{
    switch (type) {
    case InlineMessage::Type::Positive:
        return QIcon::fromTheme(QStringLiteral("emblem-success"),
                                QIcon::fromTheme(QStringLiteral("dialog-ok")));
    case InlineMessage::Type::Warning:
        return QIcon::fromTheme(QStringLiteral("emblem-warning"),
                                style->standardIcon(QStyle::SP_MessageBoxWarning));
    case InlineMessage::Type::Error:
        return QIcon::fromTheme(QStringLiteral("emblem-error"),
                                style->standardIcon(QStyle::SP_MessageBoxCritical));
    case InlineMessage::Type::Information:
        break;
    }
    return QIcon::fromTheme(QStringLiteral("emblem-information"),
                            style->standardIcon(QStyle::SP_MessageBoxInformation));
}

} // namespace

InlineMessage::InlineMessage(QWidget *parent)
    : QFrame(parent)
{
    setFrameShape(QFrame::NoFrame);
    setAutoFillBackground(false);
    setAttribute(Qt::WA_StyledBackground, false);

    // Kirigami: padding smallSpacing, the icon inset another smallSpacing,
    // largeSpacing between icon and text, smallSpacing before the close button.
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(smallSpacing() * 2, smallSpacing(), smallSpacing(), smallSpacing());
    layout->setSpacing(smallSpacing());

    m_icon = new QLabel(this);
    m_icon->setObjectName(QStringLiteral("inlineMessageIcon"));
    m_icon->setFixedSize(kIconSize, kIconSize);
    m_icon->setAttribute(Qt::WA_StyledBackground, false);
    layout->addWidget(m_icon, 0, Qt::AlignVCenter);
    layout->addSpacing(largeSpacing() - smallSpacing());

    m_label = new QLabel(this);
    m_label->setWordWrap(true);
    m_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_label->setAttribute(Qt::WA_StyledBackground, false);
    layout->addWidget(m_label, 1);

    m_actions = new QHBoxLayout;
    m_actions->setContentsMargins(0, 0, 0, 0);
    m_actions->setSpacing(smallSpacing());
    layout->addLayout(m_actions);

    m_close = new QToolButton(this);
    m_close->setAutoRaise(true);
    m_close->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_close->setIcon(QIcon::fromTheme(QStringLiteral("dialog-close"),
                                      style()->standardIcon(QStyle::SP_TitleBarCloseButton)));
    m_close->setToolTip(QStringLiteral("Close"));
    m_close->setAccessibleName(QStringLiteral("Close"));
    m_close->hide();
    connect(m_close, &QToolButton::clicked, this, &QWidget::hide);
    layout->addWidget(m_close, 0, Qt::AlignVCenter);

    refreshIcon();
}

void InlineMessage::setType(Type type)
{
    if (m_type == type) {
        return;
    }
    m_type = type;
    refreshIcon();
    update();
}

void InlineMessage::setPosition(Position position)
{
    m_position = position;
    update();
}

void InlineMessage::setText(const QString &text)
{
    m_label->setText(text);
}

void InlineMessage::addAction(QWidget *action)
{
    action->setParent(this);
    m_actions->addWidget(action, 0, Qt::AlignVCenter);
}

void InlineMessage::setCloseButtonVisible(bool visible)
{
    m_close->setVisible(visible);
}

QColor InlineMessage::typeColor() const
{
    const QPalette &colors = palette();
#ifdef SPEECHER_WITH_KCOLORSCHEME
    const KColorScheme scheme(colors.currentColorGroup(), KColorScheme::View);
    switch (m_type) {
    case Type::Positive:
        return scheme.foreground(KColorScheme::PositiveText).color();
    case Type::Warning:
        return scheme.foreground(KColorScheme::NeutralText).color();
    case Type::Error:
        return scheme.foreground(KColorScheme::NegativeText).color();
    case Type::Information:
        return scheme.foreground(KColorScheme::ActiveText).color();
    }
#else
    // Breeze's scheme colours, for hosts without a colour scheme to ask.
    switch (m_type) {
    case Type::Positive:
        return QColor(0x27, 0xae, 0x60);
    case Type::Warning:
        return QColor(0xf6, 0x74, 0x00);
    case Type::Error:
        return QColor(0xda, 0x44, 0x53);
    case Type::Information:
        return colors.color(QPalette::Highlight);
    }
#endif
    return colors.color(QPalette::Highlight);
}

void InlineMessage::paintEvent(QPaintEvent *)
{
    // Kirigami's org.kde.desktop style: a rectangle in the type colour, the
    // window colour inset by the border margins over it, then the type colour
    // again at 20% as a tint. Header messages keep only the bottom hairline.
    const bool inlinePosition = m_position == Position::Inline;
    const qreal radius = inlinePosition ? cornerRadius() : 0;
    const QRectF border = rect();
    const QRectF fill = border.adjusted(inlinePosition ? 1 : 0, 0, inlinePosition ? -1 : 0, -1);
    const QColor color = typeColor();
    QColor tint = color;
    tint.setAlphaF(0.20);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(border, radius, radius);
    painter.setBrush(palette().color(QPalette::Window));
    painter.drawRoundedRect(fill, radius * 0.6, radius * 0.6);
    painter.setBrush(tint);
    painter.drawRoundedRect(fill, radius * 0.6, radius * 0.6);
}

void InlineMessage::changeEvent(QEvent *event)
{
    QFrame::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) {
        refreshIcon();
        update();
    }
}

void InlineMessage::refreshIcon()
{
    m_icon->setPixmap(typeIcon(m_type, style()).pixmap(kIconSize, kIconSize));
}

} // namespace speecher
