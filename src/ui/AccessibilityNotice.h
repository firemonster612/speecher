#pragma once

#include <QFrame>

class QLabel;
class QPushButton;

namespace speecher {

class AccessibilityNotice final : public QFrame {
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
    QLabel *m_message;
    QPushButton *m_enableButton;
    bool m_compact = false;
    bool m_supported = true;
    bool m_enabled = false;
    bool m_persistent = false;
};

} // namespace speecher
