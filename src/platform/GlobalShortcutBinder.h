#pragma once

#include <QKeySequence>
#include <QObject>
#include <QString>

namespace speecher {

// Binds one desktop-wide key sequence to dictation. Platforms that report key
// release drive push-to-talk through activated()/deactivated(); platforms that
// only report a trigger emit activated() alone.
class GlobalShortcutBinder : public QObject {
    Q_OBJECT

public:
    using QObject::QObject;

    virtual bool supported() const = 0;
    // Empty while supported(); otherwise what to tell the user.
    virtual QString unsupportedReason() const = 0;
    // Registers the binding with the desktop shortcut service. Kept out of the
    // constructor because registration costs a round trip to that service.
    virtual void bind() = 0;
    virtual QKeySequence shortcut() const = 0;
    virtual bool setShortcut(const QKeySequence &shortcut, QString *error = nullptr) = 0;

signals:
    void activated();
    void deactivated();
};

} // namespace speecher
