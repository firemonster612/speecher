#pragma once

#include "app/SettingsPageName.h"

#include <QString>

#include <optional>

namespace speecher {

// The settings pages the sidebar offers after Dictation, in sidebar order.
enum class AppPageId {
    General,
    Audio,
    Output,
    Accounts,
    Refinement,
    Vocabulary,
};

// The page a name from the command line or IPC means, case-insensitively.
// Names of pages that no longer exist still resolve, to wherever their rows
// went: the Applications page became a card on Output.
inline std::optional<AppPageId> appPageFromName(const QString &name)
{
    const QString key = canonicalSettingsPageName(name);
    if (key == QStringLiteral("general")) {
        return AppPageId::General;
    }
    if (key == QStringLiteral("audio")) {
        return AppPageId::Audio;
    }
    if (key == QStringLiteral("output")) {
        return AppPageId::Output;
    }
    if (key == QStringLiteral("accounts")) {
        return AppPageId::Accounts;
    }
    if (key == QStringLiteral("refinement")) {
        return AppPageId::Refinement;
    }
    if (key == QStringLiteral("vocabulary")) {
        return AppPageId::Vocabulary;
    }
    return std::nullopt;
}

} // namespace speecher
