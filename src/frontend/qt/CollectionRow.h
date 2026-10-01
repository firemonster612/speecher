#pragma once

#include "frontend/qt/SchemaSettingsPage.h"

#include <optional>

class QTableWidget;

namespace speecher {

// The editor every Collection row renders as: a table built from the described
// columns, with Delete and Add beneath it. Add and activating a row (a
// double-click, Return or F2) open the record dialog; toggles and choices also
// edit in place. One of these replaces each of the hand-built collection
// editors the settings pages used to carry.
SchemaCustomRow makeCollectionRow(const SettingsRow &descriptor,
                                  QWidget *parent,
                                  std::function<void()> notifyChanged);

// Edits the column's cells in a multi-line editor, for a column whose values
// may hold several lines.
void useMultilineEditor(QTableWidget *table, int column);

// Adds a record, or edits the one at row in current(), in a dialog with a
// field per column a person fills in; a multiline column gets a text box. The
// dialog keeps OK off until the first text field holds something, refuses an
// edit whose record changed or went while it was open, checks the records it
// would leave against the descriptor's validate, keeps any problems on screen,
// and hands the records to apply once there are none. A negative row adds.
void openRecordDialog(QWidget *parent,
                      const CollectionDescriptor &collection,
                      const AppSettings &appSettings,
                      qsizetype row,
                      std::function<QList<QVariantMap>()> current,
                      std::function<void(const QList<QVariantMap> &)> apply);

// Asks for a file, parses it the way the descriptor says, and merges what it
// holds into the records already there. Returns nothing when the reader
// cancelled or the file was refused, having already said why.
std::optional<QList<QVariantMap>> importedRecords(QWidget *parent,
                                                  const CollectionDescriptor &collection,
                                                  const QList<QVariantMap> &current);

} // namespace speecher
