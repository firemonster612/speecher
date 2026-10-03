#pragma once

#include "frontend/qt/SchemaSettingsPage.h"

namespace speecher {

class ApplicationController;

// The Test microphone row: a level meter and the button that starts and stops
// a MicrophoneTest. Leaving the page or closing the window hides the row,
// which ends the test.
SchemaCustomRowFactory microphoneTestRow(ApplicationController &controller);

} // namespace speecher
