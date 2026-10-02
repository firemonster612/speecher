#include "ui/AccessibilityNotice.h"

#include "app/AccessibilityPresentation.h"

#include <QLabel>
#include <QPushButton>

namespace speecher {

AccessibilityNotice::AccessibilityNotice(QWidget *parent)
    : InlineMessage(parent)
    , m_enableButton(new QPushButton(this))
{
    setObjectName(QStringLiteral("accessibilityNotice"));
    setType(Type::Warning);
    label()->setObjectName(QStringLiteral("accessibilityNoticeMessage"));
    m_enableButton->setObjectName(QStringLiteral("enableAccessibilityButton"));
    addAction(m_enableButton);

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

    label()->setText(accessibilityNoticeText(enabled, m_compact));
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
    label()->setText(QStringLiteral("Could not open Accessibility settings: %1").arg(message));
#elif defined(Q_OS_WIN)
    label()->setText(QStringLiteral("UI Automation is unavailable: %1").arg(message));
#else
    label()->setText(QStringLiteral("Could not enable desktop accessibility: %1").arg(message));
#endif
    show();
}

} // namespace speecher
