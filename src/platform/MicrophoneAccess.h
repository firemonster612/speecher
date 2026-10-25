#pragma once

namespace speecher {

// What the computer's privacy settings say about Speecher recording.
enum class MicrophoneAccess {
    // The desktop lets every app record without asking, as Linux's does.
    NotAsked,
    Allowed,
    // macOS: nobody has answered yet; the first dictation asks.
    NotDetermined,
    // macOS: turned off for Speecher under Privacy & Security.
    Denied,
    // macOS: a profile or Screen Time blocks it, out of the person's reach.
    Restricted,
    // Windows: microphone access is off for the whole device.
    DeviceOff,
    // Windows: off for every app this person runs.
    AppsOff,
    // Windows: off for desktop apps, Speecher among them.
    DesktopAppsOff,
};

} // namespace speecher
