#pragma once

#include "output/YdotoolSetup.h"

#include <functional>

class QDialog;
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

// The explicit enable step after setup: Run test types into the dialog's field
// through typeText, and the accepting Enable button unlocks only once a test
// has passed. Exposed so tests can drive the gating without a ydotool daemon.
QDialog *createYdotoolEnableDialog(
    QWidget *parent,
    std::function<bool(const QString &text, QString *error)> typeText);

bool startYdotoolSetup(SettingsStore &settings,
                       QWidget *dialogParent,
                       YdotoolSetupFlowOptions options,
                       QObject *callbackContext,
                       std::function<void(const YdotoolSetupFlowResult &)> finished);

} // namespace speecher
