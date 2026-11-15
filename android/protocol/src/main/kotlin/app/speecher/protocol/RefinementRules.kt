package app.speecher.protocol

import java.util.UUID

// The built-in refinement rules, ported from src/core/RefinementRules.cpp: the parts of the cleanup
// levels and the format step a Writing Profile can switch, which level starts each on, and whether
// a rule, built-in or the user's, is on for a profile.

/** Where the profile screen lists a built-in rule. */
enum class RuleGroup {
    Cleanup,
    Formatting,
    More,
}

/**
 * A built-in rule. [startsOnAt] is the lowest level that starts it on, null for none. The prompt
 * adds [onSentence] when a profile turns it on although its level starts it off, and [offSentence]
 * when a profile turns it off although its level starts it on; empty where that can't happen.
 */
data class BuiltInRule(
    val id: String,
    val group: RuleGroup,
    val startsOnAt: CleanupStrength?,
    val onSentence: String = "",
    val offSentence: String = "",
)

/**
 * The rule that makes the text a prompt for a coding agent. It starts on in the built-in AI coding
 * profile, whatever the level, and adds a section of its own rather than a sentence.
 */
const val CODING_AGENT_RULE = "codingAgent"

/** Every built-in rule, in the order the prompt and the profile screen list them. */
val builtInRefinementRules: List<BuiltInRule> =
    listOf(
        BuiltInRule(
            "fillers",
            RuleGroup.Cleanup,
            CleanupStrength.Balanced,
            "Remove filler words (um, uh, you know), stutters, repeated words and false starts.",
            "Keep filler words, stutters, repeated words and false starts as spoken.",
        ),
        BuiltInRule(
            "smoothing",
            RuleGroup.Cleanup,
            CleanupStrength.Balanced,
            "Smooth a clumsy phrase when the meaning is clear.",
            "Keep clumsy phrasing as spoken, and fix only clear grammar slips.",
        ),
        BuiltInRule(
            "structure",
            RuleGroup.Cleanup,
            CleanupStrength.Balanced,
            "Start a new paragraph where the topic changes, and use a list where the speaker plainly lists items or steps.",
            "Add paragraphs or lists only where the speaker dictates them.",
        ),
        BuiltInRule(
            "rewrite",
            RuleGroup.Cleanup,
            CleanupStrength.StrongPolish,
            "Rewrite for clarity and flow: merge points that overlap, and reorder sentences when that reads better.",
            "Keep the speaker's sentences and their order rather than rewriting for flow.",
        ),
        BuiltInRule(
            "headings",
            RuleGroup.Cleanup,
            CleanupStrength.StrongPolish,
            "Add short headings where they help the reader.",
            "Add no headings unless the speaker asks for them.",
        ),
        BuiltInRule(
            "digits",
            RuleGroup.Formatting,
            CleanupStrength.LightCleanup,
            offSentence =
                "Write numbers, times, dates and amounts in words, as spoken, outside literals.",
        ),
        BuiltInRule(
            "markdown",
            RuleGroup.Formatting,
            CleanupStrength.LightCleanup,
            offSentence = "Write plain text with no Markdown.",
        ),
        BuiltInRule(
            "noEmDashes",
            RuleGroup.Formatting,
            CleanupStrength.LightCleanup,
            offSentence = "Em dashes are fine where they fit.",
        ),
        BuiltInRule(CODING_AGENT_RULE, RuleGroup.More, null),
        BuiltInRule("britishSpelling", RuleGroup.More, null, "Use British spelling."),
        BuiltInRule(
            "lowercase",
            RuleGroup.More,
            null,
            "Write everything in lowercase, except literals.",
        ),
    )

fun isBuiltInRule(ruleId: String): Boolean = builtInRefinementRules.any { it.id == ruleId }

/**
 * Whether a rule is on for [profile] at [level] before the profile switches it: a built-in rule as
 * its level starts it, None starting what Light does (the level None translates at); one of the
 * user's [rules] when it is on in every profile. An id neither built in nor in [rules] is off.
 */
fun ruleDefault(
    ruleId: String,
    profile: WritingProfile,
    level: CleanupStrength,
    rules: List<RefinementRule>,
): Boolean {
    if (ruleId == CODING_AGENT_RULE) return profile == WritingProfile.AiCoding
    val builtIn =
        builtInRefinementRules.firstOrNull { it.id == ruleId }
            ?: return rules.any { it.id == ruleId && it.everyProfile }
    return builtIn.startsOnAt != null &&
        maxOf(level, CleanupStrength.LightCleanup) >= builtIn.startsOnAt
}

/** Whether a rule is on for [profile] at [level]: the profile's own switch, else the default. */
fun ruleOn(
    ruleId: String,
    profile: WritingProfile,
    settings: WritingProfileSettings,
    level: CleanupStrength,
    rules: List<RefinementRule>,
): Boolean = settings.rules[ruleId] ?: ruleDefault(ruleId, profile, level, rules)

/** How many built-in rules the profile changed from its level's defaults. */
val WritingProfileSettings.changedBuiltInRules: Int
    get() = rules.keys.count(::isBuiltInRule)

/**
 * Without the profile's switches for built-in rules, as choosing a cleanup level does, so each
 * starts as the level starts it.
 */
fun WritingProfileSettings.withoutBuiltInRules(): WritingProfileSettings =
    copy(rules = rules.filterKeys { !isBuiltInRule(it) })

/** With [profile]'s switch for [ruleId] set to [on], dropped when it equals the default. */
fun WritingProfileSettings.withRule(
    ruleId: String,
    on: Boolean,
    profile: WritingProfile,
    rules: List<RefinementRule>,
): WritingProfileSettings =
    copy(
        rules =
            if (on == ruleDefault(ruleId, profile, cleanupStrength, rules)) this.rules - ruleId
            else this.rules + (ruleId to on),
    )

/** A new id for one of the user's rules, none of [taken]: the desktop's newCustomId. */
fun newRuleId(taken: Collection<String>): String = generateSequence {
    "custom_rule_" + UUID.randomUUID().toString().replace("-", "").take(8)
}
    .first { it !in taken }
