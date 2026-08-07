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

QList<AppRecognitionRule> builtInAppRecognitionRules()
{
    return {
        {QStringLiteral("t3code"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("chatgpt"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("codex"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("cursor"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("windsurf"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("kiro"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("zed"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("opencode"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("aider"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("claude code"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("gemini cli"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("github copilot"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("replit agent"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("amazon q developer"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("qwen code"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("mistral vibe"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("goose"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("ampcode"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("augment code"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("sourcegraph cody"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("cline"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("roo code"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("kilo code"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("factory droid"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("auggie"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("kimi code"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("pearai"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("trae"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("google antigravity"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("tabnine"), AppCategory::AiCoding, WritingProfile::Work},
        {QStringLiteral("terminal"), AppCategory::Terminal, WritingProfile::Work},
        {QStringLiteral("konsole"), AppCategory::Terminal, WritingProfile::Work},
        {QStringLiteral("ghostty"), AppCategory::Terminal, WritingProfile::Work},
        {QStringLiteral("alacritty"), AppCategory::Terminal, WritingProfile::Work},
        {QStringLiteral("kitty"), AppCategory::Terminal, WritingProfile::Work},
        {QStringLiteral("thunderbird"), AppCategory::Email, WritingProfile::Email},
        {QStringLiteral("kmail"), AppCategory::Email, WritingProfile::Email},
        {QStringLiteral("mail"), AppCategory::Email, WritingProfile::Email},
        {QStringLiteral("firefox"), AppCategory::Browser, std::nullopt},
        {QStringLiteral("chrom"), AppCategory::Browser, std::nullopt},
        {QStringLiteral("helium"), AppCategory::Browser, std::nullopt},
        {QStringLiteral("browser"), AppCategory::Browser, std::nullopt},
        {QStringLiteral("libreoffice"), AppCategory::Office, WritingProfile::Work},
        {QStringLiteral("writer"), AppCategory::Office, WritingProfile::Work},
        {QStringLiteral("office"), AppCategory::Office, WritingProfile::Work},
        {QStringLiteral("kate"), AppCategory::CodeEditor, WritingProfile::Work},
        {QStringLiteral("code"), AppCategory::CodeEditor, WritingProfile::Work},
        {QStringLiteral("editor"), AppCategory::CodeEditor, WritingProfile::Work},
        {QStringLiteral("jetbrains"), AppCategory::CodeEditor, WritingProfile::Work},
        {QStringLiteral("signal"), std::nullopt, WritingProfile::Personal},
        {QStringLiteral("discord"), std::nullopt, WritingProfile::Personal},
        {QStringLiteral("telegram"), std::nullopt, WritingProfile::Personal},
        {QStringLiteral("whatsapp"), std::nullopt, WritingProfile::Personal},
        {QStringLiteral("messenger"), std::nullopt, WritingProfile::Personal},
        {QStringLiteral("element"), std::nullopt, WritingProfile::Personal},
        {QStringLiteral("slack"), std::nullopt, WritingProfile::Work},
        {QStringLiteral("microsoft teams"), std::nullopt, WritingProfile::Work},
        {QStringLiteral("linear"), std::nullopt, WritingProfile::Work},
        {QStringLiteral("notion"), std::nullopt, WritingProfile::Work},
    };
}

static bool ruleMatches(const AppRecognitionRule &rule,
                        const Target &target,
                        bool includeWindowTitle)
{
    QStringList identityParts{
        target.applicationId,
        target.applicationName,
        target.processName,
        target.role,
    };
    if (includeWindowTitle) {
        identityParts.append(target.windowTitle);
    }
    const QString identity = identityParts.join(QLatin1Char(' '));
    if (identity.contains(rule.match, Qt::CaseInsensitive)) {
        return true;
    }
    const auto compact = [](const QString &value) {
        QString result;
        result.reserve(value.size());
        for (const QChar character : value) {
            if (character.isLetterOrNumber()) result.append(character.toCaseFolded());
        }
        return result;
    };
    return compact(identity).contains(compact(rule.match));
}

bool isTerminalTarget(const Target &target)
{
    if (target.category == AppCategory::Terminal
        || target.role.contains(QStringLiteral("terminal"), Qt::CaseInsensitive)) {
        return true;
    }
    for (const AppRecognitionRule &rule : builtInAppRecognitionRules()) {
        if (rule.category == AppCategory::Terminal && ruleMatches(rule, target, false)) {
            return true;
        }
    }
    return false;
}

AppCategory classifyTarget(const Target &target,
                           const QList<AppRecognitionRule> &customRules)
{
    for (const AppRecognitionRule &rule : customRules) {
        if (rule.category && ruleMatches(rule, target, false)) {
            return *rule.category;
        }
    }
    if (target.aiCodingToolActive) {
        return AppCategory::AiCoding;
    }
    for (const AppRecognitionRule &rule : builtInAppRecognitionRules()) {
        if (rule.category && ruleMatches(rule, target, false)) {
            return *rule.category;
        }
    }
    return target.hasIdentity() ? AppCategory::General : AppCategory::Unknown;
}

WritingProfile inferWritingProfile(const Target &target, WritingProfile fallback)
{
    for (const AppRecognitionRule &rule : builtInAppRecognitionRules()) {
        const bool includeWindowTitle = rule.category != AppCategory::AiCoding;
        if (rule.writingProfile && ruleMatches(rule, target, includeWindowTitle)) {
            return *rule.writingProfile;
        }
    }

    switch (target.category) {
    case AppCategory::Email:
        return WritingProfile::Email;
    case AppCategory::CodeEditor:
    case AppCategory::AiCoding:
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
    return resolveWritingProfile(target, overrides, {}, fallback);
}

WritingProfile resolveWritingProfile(const Target &target,
                                     const QList<WritingProfileOverride> &overrides,
                                     const QList<AppRecognitionRule> &recognitionRules,
                                     WritingProfile fallback)
{
    for (const WritingProfileOverride &override : overrides) {
        if (override.enabled
            && !override.applicationId.trimmed().isEmpty()
            && override.applicationId.compare(target.applicationId, Qt::CaseInsensitive) == 0) {
            return override.profile;
        }
    }
    for (const AppRecognitionRule &rule : recognitionRules) {
        if (rule.writingProfile && ruleMatches(rule, target, true)) {
            return *rule.writingProfile;
        }
    }
    return inferWritingProfile(target, fallback);
}

} // namespace speecher
