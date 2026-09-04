#pragma once

#include <QString>

namespace speecher::Theme {

void apply(const QString &theme);

// False once a Light or Dark request was applied and the platform kept its own
// colour scheme; true until then, and after a request that took effect.
bool overrideHonored();

}
