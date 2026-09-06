#pragma once

#include <QChar>
#include <Qt>

#include <optional>

namespace speecher::mac {

// Qt's ControlModifier is Command on macOS. Command can select a different
// layout, as with Dvorak-QWERTY Cmd, so it must participate in translation.
std::optional<quint16> keyCodeForCharacter(QChar character, Qt::KeyboardModifiers modifiers);

} // namespace speecher::mac
