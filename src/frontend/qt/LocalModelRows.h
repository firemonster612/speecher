#pragma once

#include "core/LocalModelCatalog.h"
#include "frontend/qt/SchemaSettingsPage.h"
#include "ui/InsightsCharts.h"

namespace speecher {

class LocalSetup;

// The Local models page's list and detail ("localModelBrowser"): the catalog
// in a list view, the selected model's facts and buttons beside it. Its value
// is the model dictation uses, empty while another provider transcribes.
SchemaCustomRowFactory localModelRows(LocalSetup &setup);

// A rating's badge colour, shared with the setup assistant's comparison:
// Recommended in the accent, Not recommended in the negative tone, the rest
// neutral.
Badge::Tone modelRatingTone(ModelRating rating);

} // namespace speecher
