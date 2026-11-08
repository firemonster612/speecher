#pragma once

#include "core/Apps.h"

#include <QSettings>

// How the person's Apps and the App Types' settings are stored: apps/entries
// and apps/types, each a JSON array, where a value left out is unset. Nothing
// stored, or no list, reads as no apps and the default App Types; a type the
// list leaves out reads as its default.

namespace speecher::AppsSettingsCodec {

AppsSettings load(const QSettings &settings);
void store(QSettings &settings, const AppsSettings &apps);

} // namespace speecher::AppsSettingsCodec
