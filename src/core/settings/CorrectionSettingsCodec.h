#pragma once

#include "core/LearnedCorrection.h"

#include <QSettings>

namespace speecher::CorrectionSettingsCodec {

bool learningEnabled(const QSettings &settings);
void storeLearningEnabled(QSettings &settings, bool value);
QList<LearnedCorrection> load(const QSettings &settings);
void store(QSettings &settings, const QList<LearnedCorrection> &corrections);
bool add(QSettings &settings, const QString &original, const QString &corrected,
         const QString &applicationId, double confidence);
void setEnabled(QSettings &settings, const QString &id, bool enabled);
void remove(QSettings &settings, const QString &id);

} // namespace speecher::CorrectionSettingsCodec
