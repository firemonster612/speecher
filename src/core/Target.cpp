#include "core/Target.h"

#include <QStringList>

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
    return AppCategory::Unknown;
}

QString writingProfileName(WritingProfile profile)
{
    switch (profile) {
    case WritingProfile::Work:
        return QStringLiteral("work");
    case WritingProfile::Email:
        return QStringLiteral("email");
    case WritingProfile::Personal:
        return QStringLiteral("personal");
    case WritingProfile::Other:
        return QStringLiteral("other");
    }
    return QStringLiteral("other");
}

WritingProfile writingProfileFromName(const QString &name)
{
    const QString normalized = name.trimmed().toLower();
    if (normalized == QStringLiteral("work") || normalized == QStringLiteral("technical")) {
        return WritingProfile::Work;
    }
    if (normalized == QStringLiteral("email")) {
        return WritingProfile::Email;
    }
    if (normalized == QStringLiteral("personal")) {
        return WritingProfile::Personal;
    }
    return WritingProfile::Other;
}

QList<WritingProfileSettings> defaultWritingProfileSettings()
{
    return {
        {WritingProfile::Work, QStringLiteral("balanced"), QStringLiteral("none")},
        {WritingProfile::Email, QStringLiteral("balanced"), QStringLiteral("none")},
        {WritingProfile::Personal, QStringLiteral("balanced"), QStringLiteral("none")},
        {WritingProfile::Other, QStringLiteral("balanced"), QStringLiteral("none")},
    };
}

WritingProfileSettings writingProfileSettingsFor(const QList<WritingProfileSettings> &settings,
                                                  WritingProfile profile)
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

bool Target::hasIdentity() const
{
    return !applicationId.isEmpty() || processId > 0 || !applicationName.isEmpty();
}

bool Target::hasSelection() const
{
    return !secure
        && selectionStart >= 0
        && selectionEnd > selectionStart
        && !selectedText.isEmpty();
}

AppCategory classifyTarget(const Target &target)
{
    const QString identity = QStringList{
        target.applicationId,
        target.applicationName,
        target.processName,
        target.role,
    }.join(QLatin1Char(' ')).toLower();

    if (identity.contains(QStringLiteral("terminal"))
        || identity.contains(QStringLiteral("konsole"))
        || identity.contains(QStringLiteral("ghostty"))
        || identity.contains(QStringLiteral("alacritty"))
        || identity.contains(QStringLiteral("kitty"))) {
        return AppCategory::Terminal;
    }
    if (identity.contains(QStringLiteral("thunderbird"))
        || identity.contains(QStringLiteral("kmail"))
        || identity.contains(QStringLiteral("mail"))) {
        return AppCategory::Email;
    }
    if (identity.contains(QStringLiteral("firefox"))
        || identity.contains(QStringLiteral("chrom"))
        || identity.contains(QStringLiteral("helium"))
        || identity.contains(QStringLiteral("browser"))) {
        return AppCategory::Browser;
    }
    if (identity.contains(QStringLiteral("libreoffice"))
        || identity.contains(QStringLiteral("writer"))
        || identity.contains(QStringLiteral("office"))) {
        return AppCategory::Office;
    }
    if (identity.contains(QStringLiteral("kate"))
        || identity.contains(QStringLiteral("code"))
        || identity.contains(QStringLiteral("editor"))
        || identity.contains(QStringLiteral("jetbrains"))) {
        return AppCategory::CodeEditor;
    }
    return target.hasIdentity() ? AppCategory::General : AppCategory::Unknown;
}

WritingProfile inferWritingProfile(const Target &target, WritingProfile fallback)
{
    const QString identity = QStringList{
        target.applicationId,
        target.applicationName,
        target.processName,
        target.windowTitle,
    }.join(QLatin1Char(' ')).toLower();
    if (identity.contains(QStringLiteral("signal"))
        || identity.contains(QStringLiteral("discord"))
        || identity.contains(QStringLiteral("telegram"))
        || identity.contains(QStringLiteral("whatsapp"))
        || identity.contains(QStringLiteral("messenger"))
        || identity.contains(QStringLiteral("element"))) {
        return WritingProfile::Personal;
    }
    if (identity.contains(QStringLiteral("slack"))
        || identity.contains(QStringLiteral("microsoft teams"))
        || identity.contains(QStringLiteral("linear"))
        || identity.contains(QStringLiteral("notion"))) {
        return WritingProfile::Work;
    }

    switch (target.category) {
    case AppCategory::Email:
        return WritingProfile::Email;
    case AppCategory::CodeEditor:
    case AppCategory::Terminal:
    case AppCategory::Office:
        return WritingProfile::Work;
    case AppCategory::Unknown:
    case AppCategory::General:
    case AppCategory::Browser:
        return fallback;
    }
    return fallback;
}

WritingProfile resolveWritingProfile(const Target &target,
                                     const QList<WritingProfileOverride> &overrides,
                                     WritingProfile fallback)
{
    for (const WritingProfileOverride &override : overrides) {
        if (override.enabled
            && !override.applicationId.trimmed().isEmpty()
            && override.applicationId.compare(target.applicationId, Qt::CaseInsensitive) == 0) {
            return override.profile;
        }
    }
    return inferWritingProfile(target, fallback);
}

} // namespace speecher
