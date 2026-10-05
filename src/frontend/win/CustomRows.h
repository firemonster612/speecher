#pragma once

#include "core/settings/FallbackPresentation.h"
#include "core/settings/ProviderRatings.h"
#include "core/settings/SettingsSchema.h"

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#pragma pop_macro("GetCurrentTime")

#include <functional>

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
// credential field, the CLI Proxy text rows, the profile grid, the release
// notes and the Local models browser. Null for an id this front end does not
// know.
winrt::Microsoft::UI::Xaml::UIElement customRowElement(const RowSnapshot &row,
                                                       PaneHost &host);

// Ends the Test microphone row's test and cuts it off from the pane it drew
// into. A click may still hold the test inside its start(), so dropping the
// host's reference alone would leave it running.
void endMicrophoneTest(PaneHost &host);

// The ordered fallbacks as the Fallbacks subpage and the setup assistant show
// them: the heading and subtitle, one card with a row per fallback (Move up,
// Move down and Remove) and the Add row, and the footer. Each edit applies
// to `settings` as they are at the click and hands `write` the role's new
// list; empty while the list has no heading.
winrt::Microsoft::UI::Xaml::UIElement fallbackListElement(
    ProviderRole role,
    const FallbackListPresentation &list,
    const std::function<AppSettings()> &settings,
    const std::function<void(const QStringList &)> &write,
    PaneHost &host);

// A provider's rating as the setup options and the Settings Rating row show
// it: each bar WinUI's ProgressBar out of 10 between its measure and
// ratingValueText, and only the "?" where a bar has no figure. Side by side
// under a setup option, stacked on a Rating row's control side.
winrt::Microsoft::UI::Xaml::Controls::Grid ratingBarsElement(
    const QList<Rating> &bars,
    winrt::Microsoft::UI::Xaml::Controls::Orientation orientation,
    const PaneHost &host);

// The models behind a provider, as its Advanced disclosure lists them: each
// name with its note under it, and, for models with bars of their own, the
// bars in columns under a Model, Accuracy and Speed heading.
winrt::Microsoft::UI::Xaml::Controls::StackPanel ratedModelsElement(const QList<RatedModel> &models,
                                                                    const PaneHost &host);

// Whether a Custom row lays out its own heading, card and footer, as the
// fallback lists do, rather than sitting in a card.
bool customRowIsSection(const QString &rowId);

// Whether a Custom row takes the whole card width instead of the control
// column: the profile grid, the release notes and the Local models browser.
bool customRowIsFullWidth(const QString &rowId);

} // namespace win
} // namespace speecher
