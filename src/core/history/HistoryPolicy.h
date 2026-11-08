#pragma once

// How much of one dictation History keeps: What Speecher Keeps, lowered by
// the App or App Type and by a password field.

#include "core/Apps.h"
#include "core/history/HistoryEntry.h"

namespace speecher {

struct HistorySettings {
    // A new install keeps text; migrateHistory keeps an upgrade at Stats.
    HistoryKeep keeps = HistoryKeep::Text;
    // Days a dictation's or file's text and audio stay; 0: until deleted.
    int textDays = 90;
    // Days its audio stays, never longer than its text; -1: as long as text.
    int audioDays = 7;

    bool operator==(const HistorySettings &other) const = default;
};

// The least of What Speecher Keeps, the app's own choice and, for a password
// field, Stats.
HistoryKeep historyKeep(const HistorySettings &settings, const AppResolution &app, const Target &target);

} // namespace speecher
