#pragma once

#include "dictation/DictationPorts.h"
#include "platform/atspi/AtSpiAccess.h"

namespace speecher::atspi {

class TargetSnapshot {
public:
    static TargetSnapshot capture();

    const Target &target() const;
    bool valid() const;
    bool matches(const Target &target, bool requireFocus) const;
    bool canInsert(const Target &target) const;
    bool insert(const Target &target, const QString &plainText, QString *error) const;
    QString insertionWindow(int insertionOffset, int textLength) const;
    QString correctionWindow(const Target &target,
                             const QString &original,
                             const QString &prefix,
                             const QString &suffix) const;
    bool isFocusedText() const;

private:
    Target m_target;
    AccessibleHandle m_accessible;
};

} // namespace speecher::atspi
