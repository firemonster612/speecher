package app.speecher.protocol

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

/** Expected prompts are dictationRefinementSystemPrompt output from the desktop build. */
class TargetTest {
    private fun desktopPrompt(name: String) =
        javaClass.getResource("/refinement-prompt/$name.txt")!!.readText()

    private fun resolve(
        id: String,
        name: String,
        textBeforeCaret: String? = null,
        profiles: Map<WritingProfile, WritingProfileSettings> = emptyMap(),
    ) = resolveRefinementContext(id, name, null, textBeforeCaret, WritingProfile.Other, profiles)

    @Test
    fun `light cleanup for an unknown target omits balanced rules and caret text`() {
        val context =
            resolve(
                "",
                "",
                profiles =
                    mapOf(
                        WritingProfile.Other to WritingProfileSettings(CleanupStrength.LightCleanup)
                    ),
            )
        assertEquals(desktopPrompt("light-unknown"), dictationSystemPrompt(context))
    }

    @Test
    fun `strong polish in ChatGPT adds strong and AI coding rules`() {
        val context =
            resolve(
                "com.openai.chatgpt",
                "ChatGPT",
                profiles =
                    mapOf(
                        WritingProfile.AiCoding to
                            WritingProfileSettings(CleanupStrength.StrongPolish)
                    ),
            )
        assertEquals(desktopPrompt("strong-chatgpt"), dictationSystemPrompt(context))
    }

    @Test
    fun `profile tone and text before the caret reach the context object`() {
        val context =
            resolve(
                "com.google.android.apps.messaging",
                "Messages",
                "Dinner plan",
                mapOf(WritingProfile.Other to WritingProfileSettings(tone = Tone.Casual)),
            )
        assertEquals(desktopPrompt("balanced-messages"), dictationSystemPrompt(context))
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
    fun `the platform category applies only when no rule names the app`() {
        assertEquals(
            AppCategory.Email,
            resolveRefinementContext(
                    "com.google.android.gm",
                    "Gmail",
                    AppCategory.Email,
                    null,
                    WritingProfile.Other,
                    emptyMap(),
                )
                .category,
        )
        assertEquals(
            AppCategory.AiCoding,
            resolveRefinementContext(
                    "com.openai.chatgpt",
                    "ChatGPT",
                    AppCategory.General,
                    null,
                    WritingProfile.Other,
                    emptyMap(),
                )
                .category,
        )
    }
}
