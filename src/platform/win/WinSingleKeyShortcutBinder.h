#pragma once

#include "platform/SingleKeyShortcutBinder.h"

#include <windows.h>

class WinPlatformTests;

namespace speecher {

// Watches one physical key through raw input on a message-only window, which
// RegisterHotKey cannot do: a single key is not a hot key to Windows. Raw
// input reports the scancode identity the vocabulary's win column keys on
// (make code plus the E0 byte), arrives whatever window has focus
// (RIDEV_INPUTSINK), and involves no hook or grab, so the key keeps doing
// its normal job as well.
//
// Raw input registration is per process and usage, so the stream comes from
// the shared listener in WinRawKeyboard, which the combination binders' release
// detection also reads.
class WinSingleKeyShortcutBinder final : public SingleKeyShortcutBinder {
    Q_OBJECT

public:
    explicit WinSingleKeyShortcutBinder(
        GlobalShortcutAction action = actionFor(GlobalShortcutRole::Dictation),
        QObject *parent = nullptr);

    bool supported() const override;
    QString unsupportedBindingReason(const ShortcutBinding &binding) const override;

protected:
    QString watch(const PhysicalKey &key) override;
    void unwatch() override;

private:
    friend class ::WinPlatformTests;
    void handleRawInput(const RAWINPUT &input);

    // The vocabulary's win value while watching, 0 while not.
    int m_scancode = 0;
};

} // namespace speecher
