#pragma once

// What one edit after pasting shows Speecher should have written instead:
// the evidence a Learned Correction is learned from.

#include <QString>

#include <optional>

namespace speecher {

struct CorrectionEvidence {
    QString original;
    QString corrected;
    double confidence = 0.0;
};

std::optional<CorrectionEvidence> analyzeCorrection(const QString &inserted,
                                                    const QString &edited);

} // namespace speecher
