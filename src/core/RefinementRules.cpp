#include "core/RefinementRules.h"

#include <algorithm>

namespace speecher {

namespace {

const QString kLight = QStringLiteral("light_cleanup");
const QString kMedium = QStringLiteral("balanced");
const QString kHigh = QStringLiteral("strong_polish");

// A level's place from Light up, None counting as Light; 0 for an id that
// is no level.
int levelRank(const QString &level)
{
    if (level == kHigh) {
        return 3;
    }
    if (level == kMedium) {
        return 2;
    }
    return level == kLight || level == QStringLiteral("none") ? 1 : 0;
}

} // namespace

const QList<BuiltInRule> &builtInRules()
{
    static const QList<BuiltInRule> rules{
        {QStringLiteral("fillers"), QStringLiteral("cleanup"), kMedium,
         QStringLiteral("Remove filler words (um, uh, you know), stutters, repeated words and false starts."),
         QStringLiteral("Keep filler words, stutters, repeated words and false starts as spoken.")},
        {QStringLiteral("smoothing"), QStringLiteral("cleanup"), kMedium,
         QStringLiteral("Smooth a clumsy phrase when the meaning is clear."),
         QStringLiteral("Keep clumsy phrasing as spoken, and fix only clear grammar slips.")},
        {QStringLiteral("structure"), QStringLiteral("cleanup"), kMedium,
         QStringLiteral("Start a new paragraph where the topic changes, and use a list where the speaker plainly "
                        "lists items or steps."),
         QStringLiteral("Add paragraphs or lists only where the speaker dictates them.")},
        {QStringLiteral("rewrite"), QStringLiteral("cleanup"), kHigh,
         QStringLiteral("Rewrite for clarity and flow: merge points that overlap, and reorder sentences when that "
                        "reads better."),
         QStringLiteral("Keep the speaker's sentences and their order rather than rewriting for flow.")},
        {QStringLiteral("headings"), QStringLiteral("cleanup"), kHigh,
         QStringLiteral("Add short headings where they help the reader."),
         QStringLiteral("Add no headings unless the speaker asks for them.")},
        {QStringLiteral("digits"), QStringLiteral("formatting"), kLight, QString(),
         QStringLiteral("Write numbers, times, dates and amounts in words, as spoken, outside literals.")},
        {QStringLiteral("markdown"), QStringLiteral("formatting"), kLight, QString(),
         QStringLiteral("Write plain text with no Markdown.")},
        {QStringLiteral("noEmDashes"), QStringLiteral("formatting"), kLight, QString(),
         QStringLiteral("Em dashes are fine where they fit.")},
        {kCodingAgentRule, QStringLiteral("more"), QString(), QString(), QString()},
        {QStringLiteral("britishSpelling"), QStringLiteral("more"), QString(),
         QStringLiteral("Use British spelling."), QString()},
        {QStringLiteral("lowercase"), QStringLiteral("more"), QString(),
         QStringLiteral("Write everything in lowercase, except literals."), QString()},
    };
    return rules;
}

bool isBuiltInRule(const QString &ruleId)
{
    const QList<BuiltInRule> &rules = builtInRules();
    return std::any_of(rules.cbegin(), rules.cend(), [&ruleId](const BuiltInRule &rule) { return rule.id == ruleId; });
}

bool ruleExists(const QString &ruleId, const QList<RefinementRule> &rules)
{
    return isBuiltInRule(ruleId)
        || std::any_of(rules.cbegin(), rules.cend(), [&ruleId](const RefinementRule &rule) { return rule.id == ruleId; });
}

bool ruleDefault(const QString &ruleId, const WritingProfileSettings &profile, const QString &level,
                 const QList<RefinementRule> &rules)
{
    if (ruleId == kCodingAgentRule) {
        return profile.profile == WritingProfile::AiCoding;
    }
    for (const BuiltInRule &rule : builtInRules()) {
        if (rule.id == ruleId) {
            return !rule.startsOnAt.isEmpty() && levelRank(level) >= levelRank(rule.startsOnAt);
        }
    }
    return std::any_of(rules.cbegin(), rules.cend(),
                       [&ruleId](const RefinementRule &rule) { return rule.id == ruleId && rule.everyProfile; });
}

bool ruleOn(const QString &ruleId, const WritingProfileSettings &profile, const QString &level,
            const QList<RefinementRule> &rules)
{
    const auto own = profile.rules.constFind(ruleId);
    return own != profile.rules.cend() ? *own : ruleDefault(ruleId, profile, level, rules);
}

int changedBuiltInRules(const WritingProfileSettings &profile)
{
    const QList<QString> ids = profile.rules.keys();
    return int(std::count_if(ids.cbegin(), ids.cend(), isBuiltInRule));
}

void resetBuiltInRules(WritingProfileSettings &profile)
{
    profile.rules.removeIf([](const auto &entry) { return isBuiltInRule(entry.key()); });
}

QStringList changedSteps(const WritingProfileSettings &profile, const QString &level,
                         const QList<RefinementRule> &rules)
{
    QStringList steps;
    for (const BuiltInRule &rule : builtInRules()) {
        const bool on = ruleOn(rule.id, profile, level, rules);
        const QString &sentence = on ? rule.onSentence : rule.offSentence;
        if (on != ruleDefault(rule.id, profile, level, rules) && !sentence.isEmpty()) {
            steps.append(sentence);
        }
    }
    return steps;
}

QStringList userRuleTexts(const WritingProfileSettings &profile, const QString &level,
                          const QList<RefinementRule> &rules)
{
    QStringList texts;
    for (const RefinementRule &rule : rules) {
        if (ruleOn(rule.id, profile, level, rules)) {
            const QString instruction = rule.instruction.trimmed();
            texts.append(instruction.isEmpty() ? rule.name.trimmed() : instruction);
        }
    }
    return texts;
}

} // namespace speecher
