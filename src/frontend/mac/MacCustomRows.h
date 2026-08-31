#pragma once

#include "core/settings/SettingsSchema.h"

namespace speecher {

class SettingsStore;

namespace mac {

// The parts of a Custom row the schema leaves to the front end. Qt answers the
// same questions by building widgets by hand in OutputCustomRows and
// ProviderCustomRows; a renderer over the schema only needs the choices, so on
// macOS that is all there is.
//
// Empty for a Custom row that is not a picker, such as the credential field.
QList<RowOption> customRowOptions(const QString &rowId,
                                  const AppSettings &draft,
                                  const SettingsStore &store);

// The writing profile grid as a table of records: one row per profile, holding
// the cleanup strength and the optional tone. Its records replace the row's own
// QList<WritingProfileSettings> value, which no Objective-C object can carry.
CollectionDescriptor writingProfileGrid();

} // namespace mac
} // namespace speecher
