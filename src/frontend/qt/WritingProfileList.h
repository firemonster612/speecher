#pragma once

#include "frontend/qt/SchemaSettingsPage.h"

namespace speecher {

// The Writing Profiles as card rows, one button row each with the profile's
// name and what it does, then Add profile. A row opens the profile in the
// record dialog: its cleanup level, tone and instructions, and for a profile
// the person added, its name and Delete.
SchemaCustomRow makeWritingProfileList(const CollectionDescriptor &grid,
                                       QWidget *parent,
                                       std::function<void()> notifyChanged);

} // namespace speecher
