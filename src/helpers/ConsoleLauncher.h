#pragma once

#include <windows.h>

#include <string>

// speecher.com waits for the speecher.exe it starts. A run that goes on as a
// window or daemon frees its console instead of ending, and sets this event so
// the launcher stops waiting and the prompt comes back.
inline std::wstring consoleDetachedEventName(DWORD processId)
{
    return L"Local\\speecher-console-detached-" + std::to_wstring(processId);
}
