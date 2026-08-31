#pragma once

#include "platform/GlobalShortcutBinder.h"

class QAction;

namespace speecher {

class KGlobalAccelShortcutBinder final : public GlobalShortcutBinder {
    Q_OBJECT

public:
    explicit KGlobalAccelShortcutBinder(QObject *parent = nullptr);

    bool supported() const override;
    QString unsupportedReason() const override;
    void bind() override;
    QKeySequence shortcut() const override;
    bool setShortcut(const QKeySequence &shortcut, QString *error = nullptr) override;

private:
    QAction *m_action = nullptr;
};

} // namespace speecher
