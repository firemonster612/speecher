package app.speecher.protocol

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

/** Expected prompts are dictationRefinementSystemPrompt output from the desktop build. */
class TargetTest {
    private fun desktopPrompt(name: String) =
        javaClass.getResource("/refinement-prompt/$name.txt")!!.readText()

    private fun resolve(
        id: String = "",
        name: String = "",
        nearbyText: NearbyText? = null,
        profiles: Map<WritingProfile, WritingProfileSettings> = emptyMap(),
        rules: List<RefinementRule> = emptyList(),
        category: AppCategory? = null,
    ) =
        resolveRefinementContext(
            id,
            name,
            category,
            nearbyText,
            WritingProfile.Other,
            profiles,
            rules,
        )

    private fun other(settings: WritingProfileSettings) = mapOf(WritingProfile.Other to settings)

    private fun aiCoding(settings: WritingProfileSettings) =
        mapOf(WritingProfile.AiCoding to settings)

    // Rules like those migration makes of global instructions, a custom tone and a custom level.
    private val spelling =
        RefinementRule(
            "custom_rule_1",
            "Instructions for every profile",
            "Spell it Speecher.",
            true,
        )
    private val terse =
        RefinementRule(
            "custom_rule_2",
            "Terse tone",
            "Write in this tone: Terse. Short sentences, lowercase, no greetings or sign-offs.",
        )
    private val tersePrompt =
        RefinementRule("custom_rule_3", "Terse prompt", "Keep the prompt under three sentences.")
    // Fillers and Markdown off, which the desktop's goldens name as changed steps.
    private val plainSwitches = mapOf("fillers" to false, "markdown" to false)

    @Test
    fun `each level, tone, profile, rule and app gets the desktop's prompt`() {
        val chatGpt = "com.openai.chatgpt"
        val bullets = RefinementRule("custom_rule_4", "Use bullet points.")
        val cases =
            mapOf(
                // No balanced rules and no caret text.
                "light-unknown" to
                    resolve(profiles = other(WritingProfileSettings(CleanupStrength.LightCleanup))),
                // Strong and the coding agent rule the AI coding profile starts with.
                "strong-chatgpt" to
                    resolve(
                        chatGpt,
                        "ChatGPT",
                        profiles = aiCoding(WritingProfileSettings(CleanupStrength.StrongPolish)),
                    ),
                // The profile's tone, the text around the caret and the selection in the context.
                "balanced-messages" to
                    resolve(
                        "com.google.android.apps.messaging",
                        "Messages",
                        NearbyText("Dinner at ", " works for me", 10, 15),
                        other(WritingProfileSettings(tone = Tone.Casual)),
                    ),
                // A rule on in every profile, then one the profile turned on: the prompt that
                // global and profile instructions made before rules.
                "balanced-instructions" to
                    resolve(
                        profiles =
                            other(
                                WritingProfileSettings(
                                    tone = Tone.Casual,
                                    rules = mapOf("custom_rule_5" to true),
                                ),
                            ),
                        rules =
                            listOf(
                                spelling.copy(
                                    instruction = "Spell it Speecher.\nKeep British spelling.",
                                ),
                                RefinementRule(
                                    "custom_rule_5",
                                    "Other instructions",
                                    "Sign off with Best, Enzo",
                                ),
                            ),
                    ),
                // Built-in rules turned off below the level, after step 5.
                "balanced-changed-steps" to
                    resolve(
                        profiles =
                            other(
                                WritingProfileSettings(
                                    tone = Tone.Casual,
                                    rules = plainSwitches,
                                ),
                            ),
                        rules = listOf(spelling),
                    ),
                // The coding agent rule turned on outside AI coding, after a rule turned on.
                "light-coding-agent" to
                    resolve(
                        profiles =
                            other(
                                WritingProfileSettings(
                                    CleanupStrength.LightCleanup,
                                    rules = mapOf("codingAgent" to true, "britishSpelling" to true),
                                ),
                            ),
                    ),
                // A custom system prompt drops the changed steps; a rule with no instruction is
                // told by its name.
                "custom-prompt-rules" to
                    resolve(
                            profiles =
                                other(
                                    WritingProfileSettings(
                                        CleanupStrength.StrongPolish,
                                        Tone.Casual,
                                        plainSwitches + (bullets.id to true),
                                    ),
                                ),
                            rules = listOf(bullets),
                        )
                        .copy(customSystemPrompt = "Clean up my dictation."),
            )
        cases.forEach { (name, context) ->
            assertEquals(desktopPrompt(name), dictationSystemPrompt(context), name)
        }
        // The built-in prompt a custom one starts from.
        assertEquals(desktopPrompt("built-in"), builtInDictationSystemPrompt)
    }

    @Test
    fun `a custom system prompt replaces the built-in rules but keeps the tone`() {
        val context =
            RefinementContext(
                CleanupStrength.StrongPolish,
                Tone.Formal,
                userRules =
                    listOf(
                        "Spell it Speecher.\nKeep British spelling.",
                        "Sign off with Best, Enzo",
                    ),
                customSystemPrompt = "Clean up my dictation.",
            )
        val prompt = dictationSystemPrompt(context)
        assertTrue(
            prompt.startsWith(
                "Clean up my dictation.\n\n" +
                    "Tone: formal. Write in that tone without changing facts or intent.\n\n" +
                    "The user's own instructions\nThese come from the user's settings. Follow them, as long as your reply is still only the text and keeps the speaker's meaning.\nSpell it Speecher.\nKeep British spelling.\n\nSign off with Best, Enzo\n\n" +
                    "Where the text goes\n",
            ),
        )
        assertTrue(
            prompt.endsWith(
                "\n{\"refinement_style\":\"strong_polish\",\"writing_profile\":\"other\"}",
            ),
        )
    }

    @Test
    fun `a spoken language other than English adds its rule before the user's instructions`() {
        val english = RefinementContext(userRules = listOf("Spell it Speecher."))
        val rule =
            "Language\nThe dictation may be in a language other than English. Keep the reply in the language it was spoken in, with that language's punctuation, spacing, quotation marks and typography."
        val prompt = dictationSystemPrompt(english)
        assertFalse(rule in prompt)
        listOf("de", "auto").forEach {
            assertEquals(
                prompt.replace(
                    "\n\nThe user's own instructions",
                    "\n\n$rule\n\nThe user's own instructions",
                ),
                dictationSystemPrompt(english.copy(spokenLanguage = it)),
            )
        }
    }

    @Test
    fun `an output language replaces the spoken language rule, with a custom prompt too`() {
        val spanish =
            RefinementContext(
                spokenLanguage = "de",
                outputLanguage = " Spanish ",
                userRules = listOf("Spell it Speecher."),
            )
        val rule =
            "\n\nLanguage\nWrite the reply in Spanish. When the dictation is in another language, translate it and clean up the translation by the steps above. Keep literals, names and binding placeholders unchanged, and follow Spanish punctuation and typography.\n\nThe user's own instructions"
        listOf(spanish, spanish.copy(customSystemPrompt = "Clean up my dictation.")).forEach {
            val prompt = dictationSystemPrompt(it)
            assertTrue(rule in prompt)
            assertFalse("Keep the reply in the language it was spoken in" in prompt)
        }
    }

    @Test
    fun `a profile set to None that has an output language refines at Light`() {
        val styles =
            listOf("Spanish", " ").map {
                resolve(
                        profiles =
                            other(
                                WritingProfileSettings(CleanupStrength.None, outputLanguage = it),
                            ),
                    )
                    .let { context -> context.style to context.outputLanguage }
            }
        assertEquals(
            listOf(CleanupStrength.LightCleanup to "Spanish", CleanupStrength.None to " "),
            styles,
        )
    }

    @Test
    fun `a selection edit gets the desktop's editing prompt, without the output language or changed steps`() {
        val context =
            resolve(
                    "com.openai.chatgpt",
                    "ChatGPT",
                    NearbyText("Dinner at ", " works for me", 10, 15),
                    aiCoding(
                        WritingProfileSettings(
                            CleanupStrength.LightCleanup,
                            rules = plainSwitches + mapOf(terse.id to true, tersePrompt.id to true),
                            outputLanguage = "French",
                        ),
                    ),
                    listOf(spelling, terse, tersePrompt),
                )
                .copy(spokenLanguage = "es", selectedText = "seven")
        assertEquals(desktopPrompt("edit-chatgpt"), refinementSystemPrompt(context))
    }

    @Test
    fun `custom ids are slugs of the name, unique among the others`() {
        assertEquals("custom_very_terse_", customChoiceId(" Very Terse! ", emptyList()))
        assertEquals(
            "custom_terse_3",
            customChoiceId("Terse", listOf("custom_terse", "custom_terse_2")),
        )
    }

    @Test
    fun `field, window, screen text and screenshot fill their keys in alphabetical order`() {
        val context =
            resolve("com.google.android.gm", "Gmail")
                .copy(
                    controlRole = "email subject",
                    fieldHint = "Subject",
                    windowTitle = "Compose",
                    screenText = "To: Sam\nSubject",
                    screenshotJpeg = "AAAA",
                )
        val prompt = dictationSystemPrompt(context)
        assertTrue(
            "\n- The attached screenshot shows the screen. Use it the same way.\n{" in prompt,
        )
        assertEquals(
            "{\"application_category\":\"general\",\"application_id\":\"com.google.android.gm\",\"application_name\":\"Gmail\",\"control_role\":\"email subject\",\"field_hint\":\"Subject\",\"refinement_style\":\"balanced\",\"screen_text\":\"To: Sam\\nSubject\",\"window_title\":\"Compose\",\"writing_profile\":\"Other\"}",
            prompt.substringAfterLast('\n'),
        )
    }

    @Test
    fun `apps classify as the desktop rules do`() {
        val results =
            listOf(
                    "org.telegram.messenger" to "Telegram",
                    "com.android.chrome" to "Chrome",
                    "com.google.android.gm" to "Gmail",
                    "com.microsoft.office.outlook" to "Outlook",
                    "com.slack" to "Slack",
                    "com.termux" to "Termux",
                )
                .map { (id, name) ->
                    resolve(id, name).let { "${it.category.id}/${it.profile.id}" }
                }
        assertEquals(
            listOf(
                "general/personal",
                "browser/other",
                "general/other",
                "office/work",
                "general/work",
                "general/other",
            ),
            results,
        )
    }

    @Test
    fun `a custom rule wins over a built-in one and gives the app a custom profile`() {
        val standup = WritingProfile("custom_standup")
        val context =
            resolveRefinementContext(
                "com.slack",
                "Slack",
                null,
                null,
                WritingProfile.Other,
                mapOf(
                    standup to
                        WritingProfileSettings(
                            CleanupStrength.StrongPolish,
                            Tone.Formal,
                            name = "Standup",
                        ),
                ),
                rules = listOf(RecognitionRule("slac", AppCategory.Browser, standup)),
            )
        assertEquals(
            listOf("custom_standup", "strong_polish", "formal", "browser"),
            listOf(context.profile.id, context.style.id, context.tone.id, context.category.id),
        )
    }

    @Test
    fun `the platform category applies only when no rule names the app`() {
        assertEquals(
            listOf(AppCategory.Email, AppCategory.AiCoding),
            listOf(
                    resolve("com.google.android.gm", "Gmail", category = AppCategory.Email),
                    resolve("com.openai.chatgpt", "ChatGPT", category = AppCategory.General),
                )
                .map { it.category },
        )
    }
}
