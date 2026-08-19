#pragma once

#include "frontend/qt/SchemaSettingsPage.h"

namespace speecher {

// The editor every Collection row renders as: a table built from the described
// columns, with Delete and Add beneath it. One of these replaces each of the
// hand-built collection editors the settings pages used to carry.
SchemaCustomRow makeCollectionRow(const SettingsRow &descriptor,
                                  QWidget *parent,
                                  std::function<void()> notifyChanged);

} // namespace speecher
