#pragma once

// Bringing entries into History from files: the insights log History
// replaced, and the seed files screenshots and demos run on.

#include "core/history/HistoryEntry.h"

#include <QList>
#include <QString>

#include <optional>

namespace speecher {

class HistoryStore;

// The dictations of an insights log, one JSON object per line:
// {"finishedAt":"2025-10-03T09:55:00","audioMs":38000,"words":90,"app":"Thunderbird","profile":"email"}
// finishedAt is local time without a zone; profileName is a custom profile's
// name; speechProviders and refinementProviders list the services that ran.
// Each becomes a delivered dictation that kept its stats only. Unreadable
// lines are skipped; nullopt when the file can't be read at all.
std::optional<QList<HistoryEntry>> readInsightsLog(const QString &path);

// Adds an insights log's dictations to store in one go, then renames the file
// to <path>.imported so it is read once. True with nothing to do when there
// is no file; false, with the file left for the next start and nothing
// added, when it couldn't be read or added.
bool importInsightsLog(HistoryStore &store, const QString &path);

// The entries of a history seed: a JSON array of objects with kind,
// finishedAt (local, as in the insights log), audioMs, words, app, appId,
// profile, profileName, outcome, title, text and rawText, any of which may
// be left out. An entry with text keeps Text. nullopt when the file can't be
// read or isn't such an array.
std::optional<QList<HistoryEntry>> readHistorySeed(const QString &path);

} // namespace speecher
