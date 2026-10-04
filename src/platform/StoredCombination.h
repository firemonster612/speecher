#pragma once

#include "core/ShortcutBinding.h"
#include "core/settings/SettingsKeys.h"

#include <QSettings>

namespace speecher {

// A combination binder that keeps its binding in Speecher's settings shares
// the key with the router's single-key backend, which stores single keys
// there. Each side reads and clears only its own kind.

inline ShortcutBinding storedCombination(const QString &key)
{
    const QSettings settings(QString::fromLatin1(SettingsKeys::Organization),
                             QString::fromLatin1(SettingsKeys::Application));
    const ShortcutBinding stored = ShortcutBinding::fromString(settings.value(key).toString());
    return stored.isSingleKey() ? ShortcutBinding() : stored;
}

inline void storeCombination(const QString &key, const ShortcutBinding &binding)
{
    QSettings settings(QString::fromLatin1(SettingsKeys::Organization),
                       QString::fromLatin1(SettingsKeys::Application));
    if (!binding.isEmpty()) {
        settings.setValue(key, binding.toString());
    } else if (!ShortcutBinding::fromString(settings.value(key).toString()).isSingleKey()) {
        settings.remove(key);
    }
}

} // namespace speecher
