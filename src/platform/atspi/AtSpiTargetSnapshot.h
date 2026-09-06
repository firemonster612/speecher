#pragma once

#include "dictation/DictationPorts.h"
#include "platform/CorrectionDiff.h"
#include "platform/atspi/AtSpiAccess.h"

#include <optional>

namespace speecher::atspi {

class TargetSnapshot {
public:
    static TargetSnapshot capture();

    const Target &target() const;
    bool valid() const;
    bool matches(const Target &target, bool requireFocus) const;
    bool canInsert(const Target &target) const;
    bool insert(const Target &target, const QString &plainText, QString *error) const;
    std::optional<CorrectionWindow> verifiedInsertion(const QString &plainText) const;
    QString correctionWindow(const CorrectionWindow &window) const;
    bool isFocusedText() const;

private:
    int m_characterCount = -1;
    QString m_insertionPrefix;
    QString m_insertionSuffix;
    Target m_target;
    AccessibleHandle m_accessible;
};

} // namespace speecher::atspi
