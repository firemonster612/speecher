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
    virtual bool supportKnown() const { return true; }
    virtual bool usesDesktopShortcutChooser() const { return false; }
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
    // Forgets the registration the desktop keeps for Speecher, where the
    // desktop keeps one. Portal shortcuts live with the session and need no
    // removal; the default says so.
    virtual bool removeRegistration(QString *error = nullptr)
    {
        if (error) {
            *error = QStringLiteral("Your desktop keeps no shortcut registration to remove.");
        }
        return false;
    }
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
                ? QStringLiteral("Your desktop didn't say which keys it assigned.")
                : display);
    }

signals:
    void activated();
    void deactivated();
    void bindingChanged();
    void supportChanged();
    void registrationFinished(bool bound, const QString &detail);
};

} // namespace speecher
