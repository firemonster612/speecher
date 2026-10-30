#pragma once

#include "core/LearnedCorrection.h"

#include <QSettings>

namespace speecher::CorrectionSettingsCodec {

bool learningEnabled(const QSettings &settings);
void storeLearningEnabled(QSettings &settings, bool value);
QList<LearnedCorrection> load(const QSettings &settings);
void store(QSettings &settings, const QList<LearnedCorrection> &corrections);
bool recordEvidence(QSettings &settings, const CorrectionEvidence &evidence,
                    const QString &applicationId);

} // namespace speecher::CorrectionSettingsCodec
