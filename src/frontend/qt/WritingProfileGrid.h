#pragma once

#include "frontend/qt/SchemaSettingsPage.h"

namespace speecher {

// The one row in the migrated pages that no control kind describes: a cleanup
// strength and a tone for every Writing Profile, side by side.
SchemaCustomRow makeWritingProfileGrid(QWidget *parent, std::function<void()> notifyChanged);

} // namespace speecher
