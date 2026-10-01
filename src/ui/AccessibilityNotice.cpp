#include "ui/AccessibilityNotice.h"

#include "app/AccessibilityPresentation.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>

namespace speecher {

AccessibilityNotice::AccessibilityNotice(QWidget *parent)
    : QFrame(parent)
    , m_message(new QLabel(this))
    , m_enableButton(new QPushButton(this))
{
    setObjectName(QStringLiteral("accessibilityNotice"));
    setFrameShape(QFrame::StyledPanel);
    setFrameShadow(QFrame::Plain);

    m_message->setObjectName(QStringLiteral("accessibilityNoticeMessage"));
    m_message->setWordWrap(true);
    m_enableButton->setObjectName(QStringLiteral("enableAccessibilityButton"));
    m_enableButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

    auto *layout = new QHBoxLayout(this);
    layout->addWidget(m_message, 1);
    layout->addWidget(m_enableButton, 0, Qt::AlignVCenter);

    connect(m_enableButton, &QPushButton::clicked,
            this, &AccessibilityNotice::enableRequested);
    hide();
}

void AccessibilityNotice::setState(bool enabled, bool persistent)
{
    setState(true, enabled, persistent);
}

void AccessibilityNotice::setState(bool supported, bool enabled, bool persistent)
{
    m_stateKnown = true;
    m_supported = supported;
    m_enabled = enabled;
    m_persistent = persistent;
    if (!supported || (enabled && persistent)) {
        hide();
        return;
    }

    m_message->setText(accessibilityNoticeText(enabled, m_compact));
    m_enableButton->setText(accessibilityActionCaption(enabled, persistent));
    show();
}

void AccessibilityNotice::setCompact(bool compact)
{
    if (m_compact == compact) {
        return;
    }
    m_compact = compact;
    if (m_stateKnown) {
        setState(m_supported, m_enabled, m_persistent);
    }
}

void AccessibilityNotice::showError(const QString &message)
{
    if (message.isEmpty()) {
        return;
    }
#ifdef Q_OS_MACOS
    m_message->setText(QStringLiteral("Could not open Accessibility settings: %1").arg(message));
#elif defined(Q_OS_WIN)
    m_message->setText(QStringLiteral("UI Automation is unavailable: %1").arg(message));
#else
    m_message->setText(QStringLiteral("Could not enable desktop accessibility: %1").arg(message));
#endif
    show();
}

} // namespace speecher
