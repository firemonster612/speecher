#pragma once

// The backends that press the paste keys for the Press delivery step, one per
// platform, as TextDelivery's backend factory names them.
namespace speecher::PasteKeys {

inline constexpr auto Ydotool = "ydotool";
inline constexpr auto MacPaste = "mac-paste";
inline constexpr auto WinPaste = "win-paste";

// Whether pressing the keys needs the virtual keyboard, which the person sets
// up. Only Linux does; macOS and Windows send them natively.
#ifdef Q_OS_LINUX
inline constexpr bool hasVirtualKeyboard = true;
#else
inline constexpr bool hasVirtualKeyboard = false;
#endif

} // namespace speecher::PasteKeys
