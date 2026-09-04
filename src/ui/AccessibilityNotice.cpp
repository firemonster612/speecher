#include "ui/AccessibilityNotice.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>

namespace speecher {

AccessibilityNotice::AccessibilityNotice(QWidget *parent)
    : QFrame(parent)
    , m_message(new QLabel(this))
#ifdef Q_OS_MACOS
    , m_enableButton(new QPushButton(QStringLiteral("Open settings"), this))
#else
    , m_enableButton(new QPushButton(QStringLiteral("Enable permanently"), this))
#endif
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

#ifdef Q_OS_MACOS
    // macOS grants are permanent once given, so "off" is the only state to show.
    m_message->setText(m_compact
                           ? QStringLiteral("Accessibility is off. Speecher can copy but not paste.")
                           : QStringLiteral(
                                 "Accessibility is off, so Speecher can only leave your dictation on the clipboard. "
                                 "Allow Speecher under Privacy & Security, then restart it. If the toggle already "
                                 "shows Speecher on, turn it off and on again — an updated copy of Speecher does "
                                 "not inherit the old grant."));
    m_enableButton->setEnabled(true);
#else
    if (!enabled) {
        m_message->setText(m_compact
                               ? QStringLiteral("Desktop accessibility is off. Pasting into the right app and editing selected text need it.")
                               : QStringLiteral(
                                     "Desktop accessibility is off, so Speecher cannot tell which app you are in, "
                                     "paste into it, edit selected text, or learn corrections."));
        m_enableButton->setEnabled(true);
        m_enableButton->setText(QStringLiteral("Enable permanently"));
    } else {
        m_message->setText(m_compact
                               ? QStringLiteral("Desktop accessibility is on only for this session.")
                               : QStringLiteral(
                                     "Desktop accessibility is on only for this session. Enable it permanently "
                                     "so these features keep working after you sign in again."));
        m_enableButton->setEnabled(true);
        m_enableButton->setText(QStringLiteral("Enable permanently"));
    }
#endif
    show();
}

void AccessibilityNotice::setCompact(bool compact)
{
    if (m_compact == compact) {
        return;
    }
    m_compact = compact;
    m_message->setWordWrap(!compact);
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
#else
    m_message->setText(QStringLiteral("Could not enable desktop accessibility: %1").arg(message));
#endif
    show();
}

} // namespace speecher
