#pragma once

#include "platform/GlobalShortcutBinder.h"

namespace speecher {

// A session shortcut (Cancel or Pause) on a backend that can take keys from
// the desktop on demand: the keys are held only while the binder is armed and
// not suspended, so a bare key such as C or Escape types as usual the rest of
// the time. The binding lives in Speecher's settings. A subclass supplies
// taking and letting go of the keys.
class SessionShortcutBinder : public GlobalShortcutBinder {
    Q_OBJECT

public:
    using GlobalShortcutBinder::GlobalShortcutBinder;

    void bind() override;
    ShortcutBinding shortcut() const override;
    bool setShortcut(const ShortcutBinding &shortcut, QString *error = nullptr) override;
    bool removeRegistration(QString *error = nullptr) override;
    void setArmed(bool armed) override;
    void suspend() override;
    QString resume() override;

protected:
    // Takes the keys from the desktop; false when another app holds them.
    virtual bool take(const QKeySequence &keys) = 0;
    // Lets go of whatever take() holds; a no-op when it holds nothing.
    virtual void letGo() = 0;
    // The binding bind() starts from: the stored one, unless a subclass
    // migrates one from elsewhere.
    virtual ShortcutBinding storedShortcut() const;

private:
    // Takes or lets go of the keys to match the armed and suspended state.
    // Returns why taking them failed.
    QString hold();

    ShortcutBinding m_binding;
    bool m_armed = false;
    bool m_holding = false;
    int m_suspensions = 0;
};

} // namespace speecher
