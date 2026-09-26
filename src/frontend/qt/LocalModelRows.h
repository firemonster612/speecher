#pragma once

#include "frontend/qt/SchemaSettingsPage.h"

namespace speecher {

class LocalSetup;

// The Local models page's list and detail ("localModelBrowser"): the catalog
// in a list view, the selected model's facts and buttons beside it. Its value
// is the model dictation uses, empty while another provider transcribes.
SchemaCustomRowFactory localModelRows(LocalSetup &setup);

} // namespace speecher
