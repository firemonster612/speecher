#include "output/win/WinPasteDelivery.h"

#include "platform/win/WinInjectedInput.h"

#include <windows.h>

namespace speecher {

namespace {

bool keysHeld()
{
    for (const int key : {VK_CONTROL, VK_SHIFT, VK_MENU, VK_LWIN, VK_RWIN, int('V')}) {
        if (GetAsyncKeyState(key) & 0x8000) {
            return true;
        }
    }
    return false;
}

} // namespace

void WinPasteDelivery::waitForReleasedKeys()
{
    const ULONGLONG started = GetTickCount64();
    while (keysHeld()) {
        if (GetTickCount64() - started >= 250) {
            return;
        }
        // Do not pump the Qt event loop in the middle of text delivery.
        Sleep(10);
    }
}

bool WinPasteDelivery::paste(PasteMethod method,
                             const std::function<bool()> &clearToInject,
                             QString *error)
{
    const HWND foreground = GetForegroundWindow();
    // A key can be pressed after preparation. Never alter that physical state
    // or let it turn paste into a different shortcut.
    if (keysHeld()) {
        if (error) {
            *error = QStringLiteral("Release the held keys before pasting");
        }
        return false;
    }

    INPUT input[6]{};
    int count = 0;
    const auto append = [&input, &count](WORD key, DWORD flags) {
        input[count].type = INPUT_KEYBOARD;
        input[count].ki.wVk = key;
        input[count].ki.dwFlags = flags;
        // Tagged so the single-key binder can tell this injection from the
        // user's fingers; the balancing releases below copy the tag along.
        input[count].ki.dwExtraInfo = injectedInputTag;
        ++count;
    };
    append(VK_CONTROL, 0);
    if (method == PasteMethod::TerminalPaste) {
        append(VK_SHIFT, 0);
    }
    append('V', 0);
    append('V', KEYEVENTF_KEYUP);
    if (method == PasteMethod::TerminalPaste) {
        append(VK_SHIFT, KEYEVENTF_KEYUP);
    }
    append(VK_CONTROL, KEYEVENTF_KEYUP);

    if (!foreground || GetForegroundWindow() != foreground) {
        if (error) {
            *error = QStringLiteral("The focused window changed before paste");
        }
        return false;
    }
    // The foreground check above only proves stability across this function;
    // clearToInject checks the originally captured target, after all blocking
    // preparation, because a paste into the wrong window cannot be undone.
    if (clearToInject && !clearToInject()) {
        if (error) {
            *error = QStringLiteral("The active window changed or could not be verified");
        }
        return false;
    }
    const UINT sent = SendInput(count, input, sizeof(INPUT));
    if (sent == UINT(count)) {
        return true;
    }

    // A partial batch may contain a down without its matching up. Balance
    // only those keys, never keys that were held before this paste attempt.
    INPUT releases[3]{};
    UINT releaseCount = 0;
    for (UINT index = 0; index < sent; ++index) {
        if (input[index].ki.dwFlags & KEYEVENTF_KEYUP) {
            continue;
        }
        bool released = false;
        for (UINT later = index + 1; later < sent; ++later) {
            released |= input[later].ki.wVk == input[index].ki.wVk
                && (input[later].ki.dwFlags & KEYEVENTF_KEYUP);
        }
        if (!released) {
            releases[releaseCount] = input[index];
            releases[releaseCount++].ki.dwFlags = KEYEVENTF_KEYUP;
        }
    }
    if (releaseCount) {
        SendInput(releaseCount, releases, sizeof(INPUT));
    }
    if (error) {
        *error = QStringLiteral("Windows could not send the paste keystroke");
    }
    return false;
}

} // namespace speecher
