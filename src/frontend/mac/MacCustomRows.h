#pragma once

#include "core/settings/SettingsSchema.h"

namespace speecher {

class SettingsStore;

namespace mac {

// The parts of a Custom row the schema leaves to the front end. Qt answers the
// same questions by building widgets by hand in OutputCustomRows and
// ProviderCustomRows; a renderer over the schema only needs the choices, so on
// macOS that is all there is.
//
// Empty for a Custom row that is not a picker, such as the credential field.
QList<RowOption> customRowOptions(const QString &rowId,
                                  const AppSettings &draft,
                                  const SettingsStore &store);

// The Claude Code sign-in's status line, and whether it was found; empty in
// CLI Proxy API mode, which has no check to report.
struct CredentialStatus {
    QString text;
    bool ready = false;
};
CredentialStatus anthropicCredentialStatus(const AppSettings &draft,
                                           const SettingsStore &store);

} // namespace mac
} // namespace speecher
