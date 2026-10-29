#include "core/PasteRules.h"

#include <QSet>

#include <optional>

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

namespace {

// A rule's match as rules compare it: application IDs trimmed and case-folded,
// app types trimmed and lower-cased; the global rule has none.
QString comparableMatch(PasteRuleScope scope, const QString &match)
{
    switch (scope) {
    case PasteRuleScope::Application:
        return match.trimmed().toCaseFolded();
    case PasteRuleScope::Category:
        return match.trimmed().toLower();
    case PasteRuleScope::Global:
        break;
    }
    return {};
}

bool pasteRuleMatches(const PasteRule &rule, PasteRuleScope scope, const QString &match)
{
    return rule.scope == scope && comparableMatch(scope, rule.match) == comparableMatch(scope, match);
}

} // namespace

PasteRule resolvePasteRule(const QList<PasteRule> &rules, const Target &target)
{
    const auto enabledRule = [&rules](PasteRuleScope scope, const QString &match) -> std::optional<PasteRule> {
        for (const PasteRule &rule : rules) {
            if (rule.enabled && pasteRuleMatches(rule, scope, match)) {
                return rule;
            }
        }
        return std::nullopt;
    };
    if (!target.applicationId.trimmed().isEmpty()) {
        if (const auto rule = enabledRule(PasteRuleScope::Application, target.applicationId)) {
            return *rule;
        }
    }
    if (isTerminalTarget(target)) {
        if (const auto rule = enabledRule(PasteRuleScope::Category, appCategoryName(AppCategory::Terminal))) {
            return *rule;
        }
    }
    if (const auto rule = enabledRule(PasteRuleScope::Category, appCategoryName(target.category))) {
        return *rule;
    }
    return {PasteRuleScope::Global, QString(), globalPasteMethod(rules), true};
}

PasteMethod globalPasteMethod(const QList<PasteRule> &rules)
{
    for (const PasteRule &rule : rules) {
        if (rule.enabled && rule.scope == PasteRuleScope::Global) {
            return rule.method;
        }
    }
    return PasteMethod::ClipboardOnly;
}

const PasteRule *findPasteRule(const QList<PasteRule> &rules, PasteRuleScope scope, const QString &match)
{
    for (const PasteRule &rule : rules) {
        if (pasteRuleMatches(rule, scope, match)) {
            return &rule;
        }
    }
    return nullptr;
}

void setPasteRule(QList<PasteRule> &rules, PasteRuleScope scope, const QString &match, PasteMethod method)
{
    for (PasteRule &rule : rules) {
        if (pasteRuleMatches(rule, scope, match)) {
            rule.method = method;
            rule.enabled = true;
            return;
        }
    }
    rules.append({scope, match, method, true});
}

void removePasteRule(QList<PasteRule> &rules, PasteRuleScope scope, const QString &match)
{
    rules.removeIf([scope, &match](const PasteRule &rule) { return pasteRuleMatches(rule, scope, match); });
}

QStringList validatePasteRules(const QList<PasteRule> &rules)
{
    QSet<QString> applicationIds;
    for (const PasteRule &rule : rules) {
        if (rule.scope != PasteRuleScope::Application) {
            continue;
        }
        const QString id = comparableMatch(rule.scope, rule.match);
        if (applicationIds.contains(id)) {
            return {QStringLiteral("Each application ID can have only one paste rule.")};
        }
        applicationIds.insert(id);
    }
    return {};
}

} // namespace speecher
