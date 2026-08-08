#pragma once

#include <QString>

#include <optional>

namespace speecher {

struct LearnedCorrection {
    QString id;
    QString original;
    QString corrected;
    QString applicationId;
    qint64 createdAtMs = 0;
    double confidence = 0.0;
    bool enabled = true;
    int evidenceCount = 1;
    qint64 lastObservedAtMs = 0;

    bool operator==(const LearnedCorrection &other) const = default;
};

struct CorrectionEvidence {
    QString original;
    QString corrected;
    double confidence = 0.0;
};

std::optional<CorrectionEvidence> analyzeCorrection(const QString &inserted,
                                                    const QString &edited);

} // namespace speecher
