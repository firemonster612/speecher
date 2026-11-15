package app.speecher.protocol

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

/** Expected sentences are the desktop's RefinementRules.cpp. */
class RefinementRulesTest {
    @Test
    fun `a built-in rule switched away from its level adds its sentence, one left at it nothing`() {
        val switches = mapOf("fillers" to true, "digits" to false, "markdown" to true)
        val steps =
            listOf(CleanupStrength.None, CleanupStrength.StrongPolish).map {
                resolveRefinementContext(
                        "",
                        "",
                        null,
                        null,
                        WritingProfile.Other,
                        mapOf(WritingProfile.Other to WritingProfileSettings(it, rules = switches)),
                    )
                    .changedSteps
            }
        assertEquals(
            listOf(
                listOf(
                    "Remove filler words (um, uh, you know), stutters, repeated words and false starts.",
                    "Write numbers, times, dates and amounts in words, as spoken, outside literals.",
                ),
                listOf(
                    "Write numbers, times, dates and amounts in words, as spoken, outside literals.",
                ),
            ),
            steps,
        )
    }

    @Test
    fun `a switch equal to the default is dropped, and only built-in switches count and reset`() {
        val rules = listOf(RefinementRule("custom_rule_1", "Mine", everyProfile = true))
        val profile =
            WritingProfileSettings(CleanupStrength.LightCleanup)
                .withRule("fillers", true, WritingProfile.Other, rules)
                .withRule("digits", true, WritingProfile.Other, rules)
                .withRule("custom_rule_1", false, WritingProfile.Other, rules)
                .withRule("codingAgent", true, WritingProfile.AiCoding, rules)
        assertEquals(mapOf("fillers" to true, "custom_rule_1" to false), profile.rules)
        assertEquals(1, profile.changedBuiltInRules)
        assertEquals(mapOf("custom_rule_1" to false), profile.withoutBuiltInRules().rules)
    }
}
