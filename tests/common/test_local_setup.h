#pragma once

#include "app/LocalSetup.h"

namespace speecher {

// Friend seam: hands a test a hardware answer without running the probe,
// announced like the real probe's answer so connected pages react.
class LocalSetupTestAccess {
public:
    static void setHardware(LocalSetup &setup, const HardwareProfile &profile)
    {
        setup.m_hardware.profile = profile;
        setup.m_hardwareKnown = true;
        emit setup.changed();
    }
    static void setHardware(LocalSetup &setup, const HardwareSummary &hardware)
    {
        setup.m_hardware = hardware;
        setup.m_hardwareKnown = true;
        emit setup.changed();
    }
    // As a finished look for runners would leave it.
    static void setRunners(LocalSetup &setup, const QList<DetectedRunner> &runners)
    {
        setup.m_runners = runners;
        setup.m_runnersChecked = true;
        emit setup.changed();
    }
};

} // namespace speecher
