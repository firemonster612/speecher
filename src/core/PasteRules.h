#pragma once

#include "core/Target.h"

#include <QList>
#include <QString>

namespace speecher {

enum class PasteRuleScope {
    Global,
    Category,
    Application,
};

enum class PasteMethod {
    StandardPaste,
    TerminalPaste,
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

} // namespace speecher
