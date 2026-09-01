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

    static QKeySequence defaultShortcut()
    {
        return QKeySequence(Qt::META | Qt::ALT | Qt::Key_D);
    }

    virtual bool supported() const = 0;
    // Empty while supported(); otherwise what to tell the user.
    virtual QString unsupportedReason() const = 0;
    // Registers the binding with the desktop shortcut service. Kept out of the
    // constructor because registration costs a round trip to that service.
    virtual void bind() = 0;
    virtual QKeySequence shortcut() const = 0;
    virtual QString shortcutDisplay() const
    {
        return shortcut().toString(QKeySequence::NativeText);
    }
    virtual bool setShortcut(const QKeySequence &shortcut, QString *error = nullptr) = 0;
    virtual void registerShortcut()
    {
        if (!supported()) {
            emit registrationFinished(false, unsupportedReason());
            return;
        }
        bind();
        const QString display = shortcutDisplay();
        emit registrationFinished(
            !display.isEmpty(),
            display.isEmpty()
                ? QStringLiteral("The desktop did not report a Global Shortcut")
                : display);
    }

signals:
    void activated();
    void deactivated();
    void registrationFinished(bool bound, const QString &detail);
};

} // namespace speecher
