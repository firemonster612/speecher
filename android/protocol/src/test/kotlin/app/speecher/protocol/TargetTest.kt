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
        nearbyText: NearbyText? = null,
        profiles: Map<WritingProfile, WritingProfileSettings> = emptyMap(),
    ) = resolveRefinementContext(id, name, null, nearbyText, WritingProfile.Other, profiles)

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
    fun `profile tone, text around the caret and the selection reach the context object`() {
        val context =
            resolve(
                "com.google.android.apps.messaging",
                "Messages",
                NearbyText("Dinner at ", " works for me", 10, 15),
                mapOf(WritingProfile.Other to WritingProfileSettings(tone = Tone.Casual)),
            )
        assertEquals(desktopPrompt("balanced-messages"), dictationSystemPrompt(context))
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
        assertEquals(
            "{\"application_category\":\"general\",\"application_id\":\"com.google.android.gm\",\"application_name\":\"Gmail\",\"control_role\":\"email subject\",\"document_url\":\"\",\"field_hint\":\"Subject\",\"refinement_style\":\"balanced\",\"requested_tone\":\"none\",\"screen_text\":\"To: Sam\\nSubject\",\"screenshot_supplied\":true,\"window_title\":\"Compose\",\"writing_profile\":\"other\"}",
            dictationSystemPrompt(context).substringAfterLast('\n'),
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
