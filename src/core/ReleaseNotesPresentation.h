#pragma once

#include <QString>

namespace speecher {

// One release's notes as the What's New page shows them on `platform`
// ("Linux", "macOS" or "Windows"): a bullet that starts with another
// platform's name and a colon is left out with its nested bullets, one that
// starts with this platform's name loses that prefix, a heading left with no
// bullets under it goes too, and each bullet's hard-wrapped lines are joined
// into one so it wraps to the page rather than to the source file.
QString releaseNotesForPlatform(const QString &markdown, const QString &platform);
// The same for the platform this build runs on.
QString releaseNotesForThisPlatform(const QString &markdown);

} // namespace speecher
