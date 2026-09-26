#pragma once

#include "platform/GlobalShortcutBinder.h"

namespace speecher {

// A binder that observes one physical key itself, where no desktop shortcut
// service is involved. Keeps the stored binding and the down/up bookkeeping;
// a subclass supplies the observation. Combinations are not its job and are
// refused, so a router can hand those to the desktop's own service.
class SingleKeyShortcutBinder : public GlobalShortcutBinder {
    Q_OBJECT

public:
    using GlobalShortcutBinder::GlobalShortcutBinder;

    QString unsupportedReason() const override;
    void bind() override;
    ShortcutBinding shortcut() const override;
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override;
    // An empty binding stops watching and forgets the stored key.
    bool setShortcut(const ShortcutBinding &shortcut, QString *error = nullptr) override;
    void suspend() override;
    QString resume() override;

protected:
    // The binding persisted in settings, which bind() reads. A subclass with a
    // recovery path (a permission grant, a helper install) reads it to know
    // whether a stored key is waiting to be bound.
    static ShortcutBinding storedBinding();
    // Starts observing the key, replacing any earlier watch. Empty on success,
    // otherwise what to tell the user.
    virtual QString watch(const PhysicalKey &key) = 0;
    virtual void unwatch() = 0;
    // Runs before a suspension lifts, so a backend can throw away what its
    // source queued while it was looking away.
    virtual void resuming() {}
    // Called by the subclass for every observed transition of the watched
    // key; auto-repeat and suspension are filtered here. eventTimeMs is the
    // event's own timestamp on any monotonic clock, or -1 where the backend
    // has none; a press and release that both carry one report the physical
    // hold with deactivated().
    void keyDown(qint64 eventTimeMs = -1);
    void keyUp(qint64 eventTimeMs = -1);

private:
    ShortcutBinding m_binding;
    bool m_down = false;
    qint64 m_downEventTimeMs = -1;
    int m_suspensionCount = 0;
};

} // namespace speecher
