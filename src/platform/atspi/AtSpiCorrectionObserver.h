#pragma once

#include "platform/atspi/AtSpiTargetSnapshot.h"

#include <functional>

class QObject;

namespace speecher::atspi {

class CorrectionObserver {
public:
    using Observed = std::function<void(const QString &, const QString &,
                                        const QString &, double)>;

    void setEnabled(bool enabled);
    void cancel();
    void begin(CorrectionWindow window, Observed observed);
    void sample(const QString &windowText);
    void schedule(QObject *context,
                  const TargetSnapshot *snapshot,
                  CorrectionWindow window,
                  Observed observed);

private:
    quint64 m_generation = 0;
    CorrectionWindow m_window;
    Observed m_observed;
    QString m_lastEdited;
    int m_matchingSamples = 0;
    bool m_enabled = true;
    bool m_active = false;
};

} // namespace speecher::atspi
