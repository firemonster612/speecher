#include "output/win/WinPasteDelivery.h"

#include <windows.h>

namespace speecher {

bool WinPasteDelivery::paste(PasteMethod method, QString *error)
{
    INPUT input[6]{};
    int count = 0;
    const auto append = [&input, &count](WORD key, DWORD flags) {
        input[count].type = INPUT_KEYBOARD;
        input[count].ki.wVk = key;
        input[count].ki.dwFlags = flags;
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

    if (SendInput(count, input, sizeof(INPUT)) == UINT(count)) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("Windows could not send the paste keystroke");
    }
    return false;
}

} // namespace speecher
