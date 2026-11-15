#pragma once

// The built-in refinement rules: the parts of the cleanup levels and the
// format step a Writing Profile can switch, which level starts each on,
// whether a rule, built-in or the user's, is on for a profile, and what the
// rules a profile has on add to the prompt.

#include "core/Target.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace speecher {

struct BuiltInRule {
    QString id;
    // Where the editor lists it: cleanup, formatting or more.
    QString group;
    // The lowest cleanup level that starts it on (light_cleanup, balanced or
    // strong_polish); empty for none.
    QString startsOnAt;
    // What the prompt adds when the profile turns the rule on although its
    // level starts it off, and when it turns it off although the level starts
    // it on. Empty where that can't happen.
    QString onSentence;
    QString offSentence;
};

// The rule that makes the text a prompt for a coding agent. It starts on in
// the built-in AI coding profile, whatever the level, and adds a section of
// its own rather than a sentence.
inline const QString kCodingAgentRule = QStringLiteral("codingAgent");

// Every built-in rule, in the order the prompt and the editor list them.
const QList<BuiltInRule> &builtInRules();
bool isBuiltInRule(const QString &ruleId);
// Whether ruleId is a built-in rule or one of the user's `rules`.
bool ruleExists(const QString &ruleId, const QList<RefinementRule> &rules);

// Whether a rule is on for a profile at a cleanup level before the profile
// switches it: a built-in rule as its level starts it, None starting what
// Light does (the level None translates at); one of the user's `rules` when
// it is on in every profile. An id neither built in nor in `rules` is off.
bool ruleDefault(const QString &ruleId, const WritingProfileSettings &profile,
                 const QString &level, const QList<RefinementRule> &rules);
// Whether a rule is on for a profile at a level: the profile's own switch,
// else the rule's default.
bool ruleOn(const QString &ruleId, const WritingProfileSettings &profile,
            const QString &level, const QList<RefinementRule> &rules);
// How many built-in rules the profile changed from its level's defaults.
int changedBuiltInRules(const WritingProfileSettings &profile);
// Drops the profile's switches for built-in rules, as choosing a cleanup
// level does, so each starts as the level starts it.
void resetBuiltInRules(WritingProfileSettings &profile);

// The sentence of each built-in rule the profile switched away from its
// default at level, in table order, skipping rules with no such sentence.
QStringList changedSteps(const WritingProfileSettings &profile, const QString &level,
                         const QList<RefinementRule> &rules);
// What each of the user's `rules` that is on for the profile asks, in list
// order: its trimmed instruction, or its trimmed name when that is blank.
QStringList userRuleTexts(const WritingProfileSettings &profile, const QString &level,
                          const QList<RefinementRule> &rules);

} // namespace speecher
