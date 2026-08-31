#include "core/PasteRules.h"

#include <QSet>

namespace speecher {

QString pasteRuleScopeName(PasteRuleScope scope)
{
    switch (scope) {
    case PasteRuleScope::Application:
        return QStringLiteral("application");
    case PasteRuleScope::Category:
        return QStringLiteral("category");
    case PasteRuleScope::Global:
        return QStringLiteral("global");
    }
    return QStringLiteral("global");
}

PasteRuleScope pasteRuleScopeFromName(const QString &name)
{
    if (name == QStringLiteral("application")) {
        return PasteRuleScope::Application;
    }
    if (name == QStringLiteral("category")) {
        return PasteRuleScope::Category;
    }
    return PasteRuleScope::Global;
}

QString pasteMethodName(PasteMethod method)
{
    switch (method) {
    case PasteMethod::TerminalPaste:
        return QStringLiteral("terminal_paste");
    case PasteMethod::DirectInsert:
        return QStringLiteral("direct_insert");
    case PasteMethod::ClipboardOnly:
        return QStringLiteral("clipboard_only");
    case PasteMethod::StandardPaste:
        return QStringLiteral("standard_paste");
    }
    return QStringLiteral("standard_paste");
}

PasteMethod pasteMethodFromName(const QString &name)
{
    if (name == QStringLiteral("terminal_paste")) {
        return PasteMethod::TerminalPaste;
    }
    if (name == QStringLiteral("direct_insert")) {
        return PasteMethod::DirectInsert;
    }
    if (name == QStringLiteral("clipboard_only")) {
        return PasteMethod::ClipboardOnly;
    }
    return PasteMethod::StandardPaste;
}

QList<PasteRule> defaultPasteRules()
{
    return {
        {PasteRuleScope::Category, appCategoryName(AppCategory::Terminal), PasteMethod::TerminalPaste, true},
        {PasteRuleScope::Global, QString(), PasteMethod::StandardPaste, true},
    };
}

PasteRule resolvePasteRule(const QList<PasteRule> &rules, const Target &target)
{
    const QString applicationId = target.applicationId.trimmed().toCaseFolded();
    const QString category = appCategoryName(target.category);

    for (const PasteRule &rule : rules) {
        if (rule.enabled
            && rule.scope == PasteRuleScope::Application
            && !applicationId.isEmpty()
            && rule.match.trimmed().toCaseFolded() == applicationId) {
            return rule;
        }
    }
    if (isTerminalTarget(target)) {
        for (const PasteRule &rule : rules) {
            if (rule.enabled
                && rule.scope == PasteRuleScope::Category
                && rule.match.trimmed().toLower() == appCategoryName(AppCategory::Terminal)) {
                return rule;
            }
        }
    }
    for (const PasteRule &rule : rules) {
        if (rule.enabled
            && rule.scope == PasteRuleScope::Category
            && rule.match.trimmed().toLower() == category) {
            return rule;
        }
    }
    for (const PasteRule &rule : rules) {
        if (rule.enabled && rule.scope == PasteRuleScope::Global) {
            return rule;
        }
    }
    return {PasteRuleScope::Global, QString(), PasteMethod::ClipboardOnly, true};
}

QStringList validatePasteRules(const QList<PasteRule> &rules)
{
    QSet<QString> applicationIds;
    for (const PasteRule &rule : rules) {
        if (rule.scope != PasteRuleScope::Application) {
            continue;
        }
        const QString id = rule.match.toCaseFolded();
        if (applicationIds.contains(id)) {
            return {QStringLiteral("Each application ID can have only one paste rule.")};
        }
        applicationIds.insert(id);
    }
    return {};
}

} // namespace speecher
