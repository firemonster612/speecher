#pragma once

#include "core/LearnedCorrection.h"

#include <QSettings>

// Learning corrections: whether it is on, and the evidence that is not yet a
// Learned Correction (vocabulary/correctionEvidence).

namespace speecher::CorrectionSettingsCodec {

bool learningEnabled(const QSettings &settings);
void storeLearningEnabled(QSettings &settings, bool value);
// Counts one edit's evidence for a correction in the app. Evidence seen
// often enough, or sure enough, becomes a Learned Correction in the
// Vocabulary, limited to that app until the same correction is seen in
// another; evidence for one learned already adds to it. Whether the
// Vocabulary changed.
bool recordEvidence(QSettings &settings, const CorrectionEvidence &evidence,
                    const QString &applicationId);

} // namespace speecher::CorrectionSettingsCodec
