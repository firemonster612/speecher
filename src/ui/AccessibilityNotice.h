#pragma once

#include "ui/InlineMessage.h"

class QPushButton;

namespace speecher {

// The warning shown while desktop accessibility is off, with the button that
// turns it on.
class AccessibilityNotice final : public InlineMessage {
    Q_OBJECT

public:
    explicit AccessibilityNotice(QWidget *parent = nullptr);

    void setState(bool enabled, bool persistent);
    void setState(bool supported, bool enabled, bool persistent);
    void setCompact(bool compact);
    void showError(const QString &message);

signals:
    void enableRequested();

private:
    QPushButton *m_enableButton;
    bool m_compact = false;
    bool m_stateKnown = false;
    bool m_supported = true;
    bool m_enabled = false;
    bool m_persistent = false;
};

} // namespace speecher
