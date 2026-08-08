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

    bool operator==(const LearnedCorrection &other) const = default;
};

std::optional<QString> correctionBetweenAnchors(const QString &window,
                                                const QString &prefix,
                                                const QString &suffix,
                                                const QString &original);

} // namespace speecher
