#pragma once

#include "output/YdotoolSetup.h"

#include <functional>

class QObject;
class QWidget;

namespace speecher {

class SettingsStore;

struct YdotoolSetupFlowResult {
    bool helperOk = false;
    QString helperError;
    QString serviceError;
    YdotoolSetupStatus status;
};

bool startYdotoolSetup(SettingsStore &settings,
                       QWidget *dialogParent,
                       bool confirmInstall,
                       QObject *callbackContext,
                       std::function<void(const YdotoolSetupFlowResult &)> finished);

} // namespace speecher
