#pragma once

// The wire protocol between Speecher and speecher-keywatchd, and the keys the
// daemon agrees to watch. Plain C++ with no Qt: the daemon includes it inside
// its privileged boundary, the app includes it to name a key.
//
// Messages are fixed-size and carry no strings, so the daemon's parser has
// nothing to overflow. The client sends one WatchRequest; the daemon answers
// one WatchReply and, when accepted, streams KeyEvents for that key only.

#include <cstdint>
#include <string_view>

namespace speecher::keywatch {

constexpr const char *socketPath = "/run/speecher-keywatchd/socket";
constexpr std::uint8_t protocolVersion = 1;

struct WatchRequest {
    std::uint8_t version;
    std::uint8_t keyId;
};

enum class Refusal : std::uint8_t {
    None = 0,
    BadVersion = 1,
    KeyNotPermitted = 2,
    AlreadyWatching = 3,
    TooManyRequests = 4,
    NoSession = 5,
};

struct WatchReply {
    std::uint8_t version;
    std::uint8_t refusal; // A Refusal; None means the watch is on.
};

struct KeyEvent {
    std::uint8_t down; // 1 on press, 0 on release.
    std::uint8_t reserved[7];
    std::uint64_t monotonicUsec;
};

// The keys that cannot spell text. Twenty-six letters watched one at a time
// would rebuild a keylogger from harmless parts; this list is what keeps the
// parts from composing. The daemon maps an id through this table itself and
// never takes a keycode from the client.
struct PermittedKey {
    std::uint8_t id;
    std::string_view code; // KeyboardEvent.code name, as ShortcutBinding uses.
    std::uint16_t evdev;
};

constexpr PermittedKey permittedKeys[] = {
    {1, "ShiftLeft", 42},    {2, "ShiftRight", 54},   {3, "ControlLeft", 29},
    {4, "ControlRight", 97}, {5, "AltLeft", 56},      {6, "AltRight", 100},
    {7, "MetaLeft", 125},    {8, "MetaRight", 126},   {9, "CapsLock", 58},
    {10, "F13", 183},        {11, "F14", 184},        {12, "F15", 185},
    {13, "F16", 186},        {14, "F17", 187},        {15, "F18", 188},
    {16, "F19", 189},        {17, "F20", 190},        {18, "F21", 191},
    {19, "F22", 192},        {20, "F23", 193},        {21, "F24", 194},
};

constexpr const PermittedKey *permittedKeyById(std::uint8_t id)
{
    for (const PermittedKey &key : permittedKeys) {
        if (key.id == id) {
            return &key;
        }
    }
    return nullptr;
}

constexpr const PermittedKey *permittedKeyByCode(std::string_view code)
{
    for (const PermittedKey &key : permittedKeys) {
        if (key.code == code) {
            return &key;
        }
    }
    return nullptr;
}

} // namespace speecher::keywatch
