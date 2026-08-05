#pragma once

#include "dictation/DictationPorts.h"

#include <functional>

class QObject;

namespace speecher::atspi {

class TargetSnapshot;

class CorrectionObserver {
public:
    void cancel();
    void schedule(QObject *context,
                  const TargetSnapshot *snapshot,
                  const Target &target,
                  const QString &original,
                  const QString &prefix,
                  const QString &suffix,
                  std::function<void(const QString &, const QString &, const QString &, double)> observed);

private:
    quint64 m_generation = 0;
};

} // namespace speecher::atspi
