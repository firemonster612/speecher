#include "platform/win/WinSingleKeyShortcutBinder.h"

#include "platform/win/WinInjectedInput.h"
#include "platform/win/WinRawKeyboard.h"

namespace speecher {

WinSingleKeyShortcutBinder::WinSingleKeyShortcutBinder(GlobalShortcutAction action, QObject *parent)
    : SingleKeyShortcutBinder(std::move(action), parent)
{
}

bool WinSingleKeyShortcutBinder::supported() const
{
    return true;
}

QString WinSingleKeyShortcutBinder::unsupportedBindingReason(const ShortcutBinding &binding) const
{
    const QString reason = SingleKeyShortcutBinder::unsupportedBindingReason(binding);
    if (!reason.isEmpty()) {
        return reason;
    }
    if (physicalKey(binding.keyCode())->win < 0) {
        return QStringLiteral("Windows does not report the %1 key.").arg(binding.displayText());
    }
    return QString();
}

QString WinSingleKeyShortcutBinder::watch(const PhysicalKey &key)
{
    QString error;
    if (!win::listenToRawKeyboard(this, [this](const RAWINPUT &input) { handleRawInput(input); },
                                  &error)) {
        return error;
    }
    m_scancode = key.win;
    return QString();
}

void WinSingleKeyShortcutBinder::unwatch()
{
    // The raw input listener stays: the stream is shared (see the header).
    m_scancode = 0;
}

void WinSingleKeyShortcutBinder::handleRawInput(const RAWINPUT &input)
{
    if (input.header.dwType != RIM_TYPEKEYBOARD || m_scancode == 0) {
        return;
    }
    const RAWKEYBOARD &keyboard = input.data.keyboard;
    // The fake half of an escaped sequence (Pause's trailing 0x45) and
    // keyboard overrun both arrive as VKey 0xFF; neither is a key.
    if (keyboard.VKey == 0xFF) {
        return;
    }
    // Speecher's own paste injection: its WM_INPUT is queued and arrives only
    // after the delivery-scoped suspension has lifted, so the tag is what
    // keeps a binding on V or Ctrl from retriggering dictation on every paste.
    if (keyboard.ExtraInformation == injectedInputTag) {
        return;
    }
    int scancode = keyboard.MakeCode & 0x7F;
    if (keyboard.Flags & RI_KEY_E0) {
        scancode |= 0xE000;
    }
    // Raw input spells Pause as an E1-flagged 0x1D and NumLock as a bare
    // 0x45; the vocabulary holds the message-level spelling (0x45 / 0xE045).
    if (keyboard.Flags & RI_KEY_E1) {
        if (keyboard.VKey != VK_PAUSE) {
            return;
        }
        scancode = 0x45;
    } else if (scancode == 0x45) {
        scancode = 0xE045;
    }
    if (scancode != m_scancode) {
        return;
    }
    // Key repeat (repeated makes without a break) collapses in the base
    // class; on AltGr layouts the synthetic left-Ctrl make preceding right
    // Alt never reaches here for an AltRight binding, since 0x1D != 0xE038.
    if (keyboard.Flags & RI_KEY_BREAK) {
        keyUp();
    } else {
        keyDown();
    }
}

} // namespace speecher
