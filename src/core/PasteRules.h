#pragma once

#include "core/Target.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace speecher {

enum class PasteRuleScope {
    Global,
    Category,
    Application,
};

enum class PasteMethod {
    StandardPaste,
    TerminalPaste,
    DirectInsert,
    ClipboardOnly,
};

struct PasteRule {
    PasteRuleScope scope = PasteRuleScope::Global;
    QString match;
    PasteMethod method = PasteMethod::StandardPaste;
    bool enabled = true;

    bool operator==(const PasteRule &other) const = default;
};

QString pasteRuleScopeName(PasteRuleScope scope);
PasteRuleScope pasteRuleScopeFromName(const QString &name);
QString pasteMethodName(PasteMethod method);
PasteMethod pasteMethodFromName(const QString &name);
QList<PasteRule> defaultPasteRules();
PasteRule resolvePasteRule(const QList<PasteRule> &rules, const Target &target);
// The rule stored for a scope and match, such as the terminal category's, or
// nullptr. A scope's match is compared as stored; the global rule's is empty.
const PasteRule *findPasteRule(const QList<PasteRule> &rules, PasteRuleScope scope, const QString &match);
// Gives the rule for scope and match this method and turns it on, adding the
// rule where there is none.
void setPasteRule(QList<PasteRule> &rules, PasteRuleScope scope, const QString &match, PasteMethod method);
// Drops the rule for scope and match, so the wider rule decides instead.
void removePasteRule(QList<PasteRule> &rules, PasteRuleScope scope, const QString &match);
// Empty when the rules are consistent; otherwise one message per problem, ready
// to show to a person.
QStringList validatePasteRules(const QList<PasteRule> &rules);

} // namespace speecher
