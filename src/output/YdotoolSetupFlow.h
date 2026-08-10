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

struct YdotoolSetupFlowOptions {
    bool confirmInstall;
    bool applyAutomaticOutputMethod;
};

bool startYdotoolSetup(SettingsStore &settings,
                       QWidget *dialogParent,
                       YdotoolSetupFlowOptions options,
                       QObject *callbackContext,
                       std::function<void(const YdotoolSetupFlowResult &)> finished);

} // namespace speecher
