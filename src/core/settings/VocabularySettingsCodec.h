#pragma once

#include "core/AppSettings.h"

#include <QSettings>

namespace speecher::VocabularySettingsCodec {

QList<VocabularyEntry> load(const QSettings &settings);
void store(QSettings &settings, const QList<VocabularyEntry> &entries);
// Counts a use of every term the text holds. Whether it held one.
bool recordUsage(QSettings &settings, const QString &text);

} // namespace speecher::VocabularySettingsCodec
