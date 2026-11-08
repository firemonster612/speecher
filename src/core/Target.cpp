#include "core/Target.h"


#include <algorithm>

namespace speecher {

QString appCategoryName(AppCategory category)
{
    switch (category) {
    case AppCategory::General:
        return QStringLiteral("general");
    case AppCategory::Terminal:
        return QStringLiteral("terminal");
    case AppCategory::Browser:
        return QStringLiteral("browser");
    case AppCategory::Email:
        return QStringLiteral("email");
    case AppCategory::Office:
        return QStringLiteral("office");
    case AppCategory::CodeEditor:
        return QStringLiteral("code_editor");
    case AppCategory::AiCoding:
        return QStringLiteral("ai_coding");
    case AppCategory::Unknown:
        return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

AppCategory appCategoryFromName(const QString &name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("general")) {
        return AppCategory::General;
    }
    if (normalized == QStringLiteral("terminal")) {
        return AppCategory::Terminal;
    }
    if (normalized == QStringLiteral("browser")) {
        return AppCategory::Browser;
    }
    if (normalized == QStringLiteral("email")) {
        return AppCategory::Email;
    }
    if (normalized == QStringLiteral("office")) {
        return AppCategory::Office;
    }
    if (normalized == QStringLiteral("code_editor")) {
        return AppCategory::CodeEditor;
    }
    if (normalized == QStringLiteral("ai_coding")) {
        return AppCategory::AiCoding;
    }
    return AppCategory::Unknown;
}

bool isBuiltInWritingProfile(const QString &id)
{
    return id == WritingProfile::Work || id == WritingProfile::Email || id == WritingProfile::Personal
        || id == WritingProfile::AiCoding || id == WritingProfile::Other;
}

QString writingProfileFromName(const QString &name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("technical")) {
        return WritingProfile::Work;
    }
    if (isBuiltInWritingProfile(normalized) || normalized.startsWith(kCustomIdPrefix)) {
        return normalized;
    }
    return WritingProfile::Other;
}

QString writingProfileLabel(const QString &id, const QList<WritingProfileSettings> &profiles)
{
    if (id == WritingProfile::Work) return QStringLiteral("Work");
    if (id == WritingProfile::Email) return QStringLiteral("Email");
    if (id == WritingProfile::Personal) return QStringLiteral("Personal");
    if (id == WritingProfile::AiCoding) return QStringLiteral("AI coding");
    if (id == WritingProfile::Other) return QStringLiteral("Other");
    for (const WritingProfileSettings &profile : profiles) {
        if (profile.profile == id) {
            return profile.name;
        }
    }
    return {};
}

QString offeredWritingProfile(const QString &id,
                              const QList<WritingProfileSettings> &profiles,
                              const QString &fallback)
{
    if (isBuiltInWritingProfile(id)) {
        return id;
    }
    const bool held = std::any_of(profiles.cbegin(), profiles.cend(),
                                  [&id](const WritingProfileSettings &profile) { return profile.profile == id; });
    return held ? id : fallback;
}

QList<WritingProfileSettings> defaultWritingProfileSettings()
{
    return {
        {WritingProfile::Work, QStringLiteral("balanced"), QStringLiteral("none")},
        {WritingProfile::Email, QStringLiteral("balanced"), QStringLiteral("none")},
        {WritingProfile::Personal, QStringLiteral("balanced"), QStringLiteral("none")},
        {WritingProfile::AiCoding, QStringLiteral("balanced"), QStringLiteral("none")},
        {WritingProfile::Other, QStringLiteral("balanced"), QStringLiteral("none")},
    };
}

WritingProfileSettings writingProfileSettingsFor(const QList<WritingProfileSettings> &settings,
                                                  const QString &profile)
{
    for (const WritingProfileSettings &candidate : settings) {
        if (candidate.profile == profile) {
            return candidate;
        }
    }
    for (const WritingProfileSettings &candidate : defaultWritingProfileSettings()) {
        if (candidate.profile == profile) {
            return candidate;
        }
    }
    return {};
}

QString refinedCleanupLevel(const QString &level, const QString &outputLanguage)
{
    return level == QStringLiteral("none") && !outputLanguage.trimmed().isEmpty()
        ? QStringLiteral("light_cleanup")
        : level;
}

bool Target::hasIdentity() const
{
    return !applicationId.isEmpty() || processId > 0 || !applicationName.isEmpty();
}

bool Target::hasSelection() const
{
    return !secure
        && editable
        && selectionStart >= 0
        && selectionEnd > selectionStart
        && !selectedText.isEmpty();
}

void Target::setNearbyText(const QString &text, int start, int end)
{
    nearbyTextRead = true;
    nearbyTextBefore = text.left(start).right(nearbyTextChars);
    nearbyTextAfter = text.mid(end, nearbyTextChars);
    // The cut counts UTF-16 units, so it can fall inside a character such as
    // an emoji; never keep half of one.
    if (!nearbyTextBefore.isEmpty() && nearbyTextBefore.front().isLowSurrogate()) {
        nearbyTextBefore.remove(0, 1);
    }
    if (!nearbyTextAfter.isEmpty() && nearbyTextAfter.back().isHighSurrogate()) {
        nearbyTextAfter.chop(1);
    }
}

} // namespace speecher
