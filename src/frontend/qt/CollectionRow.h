#pragma once

#include "frontend/qt/SchemaSettingsPage.h"

#include <optional>

namespace speecher {

// The editor every Collection row renders as: a table built from the described
// columns, with Delete and Add beneath it. One of these replaces each of the
// hand-built collection editors the settings pages used to carry.
SchemaCustomRow makeCollectionRow(const SettingsRow &descriptor,
                                  QWidget *parent,
                                  std::function<void()> notifyChanged);

// Asks for a file, parses it the way the descriptor says, and merges what it
// holds into the records already there. Returns nothing when the reader
// cancelled or the file was refused, having already said why.
std::optional<QList<QVariantMap>> importedRecords(QWidget *parent,
                                                  const CollectionDescriptor &collection,
                                                  const QList<QVariantMap> &current);

} // namespace speecher
