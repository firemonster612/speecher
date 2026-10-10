// speecher.com: the command line's way into speecher.exe. speecher.exe is a
// GUI-subsystem program, which cmd and PowerShell do not wait for, so its exit
// status is lost and the prompt comes back first. Both try .com before .exe,
// so `speecher` runs this console program instead. It starts the speecher.exe
// beside it with the same arguments and standard handles, waits for it, and
// returns its exit status. Plain Win32, so it starts in no time.

#include "helpers/ConsoleLauncher.h"

#include <cstdio>

namespace {

// Ctrl+C goes to every process on the console, speecher.exe included, which
// decides what it means: `listen` stops recording and still prints what was
// said. This process has to outlive it to hand back its exit status. A null
// handler would do the same, but speecher.exe would inherit it and never see
// Ctrl+C at all.
BOOL WINAPI outliveInterrupt(DWORD event)
{
    return event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT;
}

std::wstring launcherPath()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (length < path.size()) {
            path.resize(length);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

// The command line after the program name, quoting and spacing kept, so
// speecher.exe parses exactly the arguments this was given. The program name
// ends at the first space or tab outside double quotes.
const wchar_t *argumentsAfterProgramName(const wchar_t *commandLine)
{
    bool quoted = false;
    for (; *commandLine != L'\0'; ++commandLine) {
        if (*commandLine == L'"') {
            quoted = !quoted;
        } else if (!quoted && (*commandLine == L' ' || *commandLine == L'\t')) {
            break;
        }
    }
    return commandLine;
}

// A console's own handles need not be inheritable, and speecher.exe would
// get none of them.
HANDLE inheritableStdHandle(DWORD stream)
{
    const HANDLE handle = GetStdHandle(stream);
    SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    return handle;
}

} // namespace

int main()
{
    const std::wstring launcher = launcherPath();
    const std::wstring app = launcher.substr(0, launcher.find_last_of(L'\\') + 1) + L"speecher.exe";
    std::wstring commandLine = L"\"" + app + L"\"" + argumentsAfterProgramName(GetCommandLineW());

    STARTUPINFOW startup{};
    startup.cb = sizeof startup;
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inheritableStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = inheritableStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = inheritableStdHandle(STD_ERROR_HANDLE);

    SetConsoleCtrlHandler(outliveInterrupt, TRUE);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(app.c_str(), commandLine.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED,
                        nullptr, nullptr, &startup, &process)) {
        std::fwprintf(stderr, L"Could not start %ls (error %lu)\n", app.c_str(), GetLastError());
        return 1;
    }
    // Made before speecher.exe runs, so it cannot detach unseen.
    const HANDLE detached = CreateEventW(nullptr, TRUE, FALSE,
                                         consoleDetachedEventName(process.dwProcessId).c_str());
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);

    const HANDLE waits[] = {process.hProcess, detached};
    if (WaitForMultipleObjects(detached ? 2 : 1, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1) {
        return 0;
    }
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    return int(exitCode);
}
