#pragma once

#include "core/settings/FallbackPresentation.h"

#include <iosfwd>

namespace speecher {

// `speecher providers`' output: a table with a header, or with json one line
// holding a JSON array, an object per provider whose unknowns are null.
void printProviderReports(const QList<ProviderReport> &reports, bool json, std::ostream &out);

// Prints every provider this build offers, judged from what the settings pages
// read without asking a server or a keyring: the saved settings, the
// downloaded Local Models and the operating system's network state. This
// process has seen no sign-in and looked for no Local Runner, so those stay
// unknown. Returns the exit code, which is 0.
int runProvidersCommand(bool json, std::ostream &out);

} // namespace speecher
