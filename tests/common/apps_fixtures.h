#pragma once

// Apps as tests set them up: one matched by id, one matched by name as a
// migrated recognition rule is, and Other apps' profile.

#include "core/Apps.h"

namespace speecher::test {

inline AppEntry idApp(const QString &id, std::optional<QString> profile = std::nullopt,
                      std::optional<PasteMethod> pasting = std::nullopt)
{
    return {QStringLiteral("app:") + id, id, {}, {{AppMatchKind::Id, id}}, std::nullopt, profile, pasting};
}

// Matched by name, and by window title too where it sets a profile.
inline AppEntry textApp(const QString &match, std::optional<AppCategory> type,
                        std::optional<QString> profile = std::nullopt)
{
    AppEntry app{QStringLiteral("app:") + match, match, {}, {{AppMatchKind::Name, match}}, type, profile, std::nullopt};
    if (profile) {
        app.matches.append({AppMatchKind::WindowTitle, match});
    }
    return app;
}

inline void setOtherAppsProfile(AppsSettings &apps, const QString &profile)
{
    for (AppTypeSettings &type : apps.types) {
        if (type.type == AppCategory::General) {
            type.writingProfile = profile;
        }
    }
}

} // namespace speecher::test
