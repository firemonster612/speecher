#pragma once

#include <QSettings>

// The migration that writes the speech and refinement Service Orders from
// the keys older releases saved them in. The old keys stay in the file,
// unwritten, so a downgrade within a release still reads them.

namespace speecher {

// Writes the speech and refinement Service Orders, Clean up my words and
// the refinement server from the keys an older release saved them in,
// once, as migration/serviceOrderVersion records. An order already saved
// stays; a file that stored none of the older keys stores no order.
void migrateServiceOrder(QSettings &settings);

} // namespace speecher
