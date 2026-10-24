#pragma once

#include "core/AppSettings.h"

#include <QString>
#include <QStringList>

#include <functional>

// Removing a custom tone or cleanup level from a draft of the settings
// together with the profile choices that named it, and putting it back; and
// the ids of what the user adds. (Saving a draft without a profile already
// drops what named the profile: see SettingsCodecs.)

namespace speecher {

// A change to a draft of the settings, such as the one that undoes a removal.
using SettingsEdit = std::function<void(AppSettings &)>;

// An id for a profile, tone or cleanup level the user adds: prefix, which
// starts with kCustomIdPrefix, then a random part. Random rather than
// counted, so an id is never given twice and an undone deletion never finds
// its id taken by something added meanwhile. Never one of `taken`.
QString newCustomId(const QString &prefix, const QStringList &taken);

// Removes the custom tone id, and every profile that chose it gets no tone.
// Returns the edit that puts the tone back where it was and points back the
// profiles that still have no tone, so changes made in between stay. Empty
// for an id the settings do not hold.
SettingsEdit removeCustomTone(AppSettings &settings, const QString &id);
// The same for a custom cleanup level. Its profiles get the built-in level it
// was based on, or Medium for one based on no level (Custom only).
SettingsEdit removeCustomCleanupLevel(AppSettings &settings, const QString &id);

} // namespace speecher
