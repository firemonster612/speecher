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
    case WritingProfile::Email:
        return QStringLiteral("email");
    case WritingProfile::Technical:
        return QStringLiteral("technical");
    case WritingProfile::Personal:
        return QStringLiteral("personal");
    case WritingProfile::General:
        return QStringLiteral("general");
    }
    return QStringLiteral("general");
}

WritingProfile writingProfileFromName(const QString &name)
{
    if (name == QStringLiteral("email")) {
        return WritingProfile::Email;
    }
    if (name == QStringLiteral("technical")) {
        return WritingProfile::Technical;
    }
    if (name == QStringLiteral("personal")) {
        return WritingProfile::Personal;
    }
    return WritingProfile::General;
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
    switch (target.category) {
    case AppCategory::Email:
        return WritingProfile::Email;
    case AppCategory::CodeEditor:
    case AppCategory::Terminal:
        return WritingProfile::Technical;
    case AppCategory::Unknown:
    case AppCategory::General:
    case AppCategory::Browser:
    case AppCategory::Office:
        return fallback;
    }
    return fallback;
}

} // namespace speecher
