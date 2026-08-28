#pragma once

#include <QString>
#include <QStringList>

namespace speecher::OutputMethod {

inline constexpr auto Automatic = "automatic";
inline constexpr auto DirectInsert = "direct_insert";
inline constexpr auto Ydotool = "ydotool";
inline constexpr auto WlCopy = "wl-copy";
inline constexpr auto MacPaste = "mac-paste";
inline constexpr auto QtClipboard = "qt-clipboard";

QStringList all();
bool isValid(const QString &method);
QString normalized(const QString &method);
QString label(const QString &method);

} // namespace speecher::OutputMethod
