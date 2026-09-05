#pragma once

#include "platform/CorrectionDiff.h"

#include <QObject>
#include <QTimer>

#include <memory>

namespace speecher {

class WinCorrectionObserver : public QObject {
public:
    explicit WinCorrectionObserver(QObject *parent = nullptr);
    ~WinCorrectionObserver() override;

    void setEnabled(bool enabled);
    void cancel();
    void observe(void *automation,
                 void *element,
                 CorrectionWindow window,
                 CorrectionTracker::Observed observed);
    void valueChanged();

private:
    struct Native;
    void sample();
    void stop();

    CorrectionTracker m_tracker;
    QTimer m_settle;
    QTimer m_deadline;
    std::unique_ptr<Native> m_native;
};

} // namespace speecher
