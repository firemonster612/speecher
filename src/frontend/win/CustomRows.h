#pragma once

#include "core/settings/SettingsSchema.h"

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher {

class SettingsStore;

namespace win {

struct PaneHost;
struct RowSnapshot;

// The parts of a Custom row the schema leaves to the front end. The mac
// renderer answers the same questions in MacCustomRows; shared options come from the settings schema and credential providers.
//
// Empty for a Custom row that is not a picker, such as the credential field.
QList<RowOption> customRowOptions(const QString &rowId,
                                  const AppSettings &draft,
                                  const SettingsStore &store);

QString anthropicCredentialStatus(const AppSettings &draft,
                                  const SettingsStore &store);



// The control for a Custom row, by id: pickers over customRowOptions, the
// credential field, the CLI Proxy text rows, the profile grid and the release
// notes. Null for an id this front end does not know.
winrt::Microsoft::UI::Xaml::UIElement customRowElement(const RowSnapshot &row,
                                                       PaneHost &host);

// Whether a Custom row takes the whole card width instead of the control
// column: the profile grid and the release notes.
bool customRowIsFullWidth(const QString &rowId);

} // namespace win
} // namespace speecher
