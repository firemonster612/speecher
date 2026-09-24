package app.speecher.protocol

// Target classification and writing profiles, ported from src/core/Target.cpp.

enum class AppCategory(val id: String) {
    General("general"),
    Terminal("terminal"),
    Browser("browser"),
    Email("email"),
    Office("office"),
    CodeEditor("code_editor"),
    AiCoding("ai_coding"),
    Unknown("unknown"),
}

enum class WritingProfile(val id: String) {
    Work("work"),
    Email("email"),
    Personal("personal"),
    AiCoding("ai_coding"),
    Other("other"),
}

enum class CleanupStrength(val id: String) {
    /** Skips refinement entirely, as the desktop does. */
    None("none"),
    LightCleanup("light_cleanup"),
    Balanced("balanced"),
    StrongPolish("strong_polish"),
}

enum class Tone(val id: String) {
    None("none"),
    Formal("formal"),
    Casual("casual"),
    VeryCasual("very_casual"),
    Excited("excited"),
    GenZ("gen_z"),
}

/** How one writing profile refines. Every profile defaults to balanced with no tone override. */
data class WritingProfileSettings(
    val cleanupStrength: CleanupStrength = CleanupStrength.Balanced,
    val tone: Tone = Tone.None,
)

/**
 * The refinement configuration and target context sent to the refiner. [textBeforeCaret] is null
 * when nearby text is excluded (context off or a secure field), which drops the caret keys.
 */
data class RefinementContext(
    val style: CleanupStrength = CleanupStrength.Balanced,
    val tone: Tone = Tone.None,
    val profile: WritingProfile = WritingProfile.Other,
    val category: AppCategory = AppCategory.Unknown,
    val applicationId: String = "",
    val applicationName: String = "",
    val textBeforeCaret: String? = null,
)

private class RecognitionRule(
    val match: String,
    val category: AppCategory?,
    val profile: WritingProfile?,
)

private val builtInRules: List<RecognitionRule> =
    listOf(
            "t3code",
            "chatgpt",
            "codex",
            "cursor",
            "windsurf",
            "kiro",
            "zed",
            "opencode",
            "aider",
            "claude code",
            "gemini cli",
            "github copilot",
            "replit agent",
            "amazon q developer",
            "qwen code",
            "mistral vibe",
            "goose",
            "ampcode",
            "augment code",
            "sourcegraph cody",
            "cline",
            "roo code",
            "kilo code",
            "factory droid",
            "auggie",
            "kimi code",
            "pearai",
            "trae",
            "google antigravity",
            "tabnine",
        )
        .map { RecognitionRule(it, AppCategory.AiCoding, WritingProfile.AiCoding) } +
        listOf(
                "terminal",
                "konsole",
                "ghostty",
                "alacritty",
                "kitty",
                "wezterm",
                "foot",
                "rio",
                "xterm",
                "urxvt",
            )
            .map { RecognitionRule(it, AppCategory.Terminal, WritingProfile.Work) } +
        listOf("thunderbird", "kmail", "mail").map {
            RecognitionRule(it, AppCategory.Email, WritingProfile.Email)
        } +
        listOf("firefox", "chrome", "chromium", "helium", "browser").map {
            RecognitionRule(it, AppCategory.Browser, null)
        } +
        listOf("libreoffice", "soffice", "writer", "office").map {
            RecognitionRule(it, AppCategory.Office, WritingProfile.Work)
        } +
        listOf("kate", "code", "editor", "jetbrains").map {
            RecognitionRule(it, AppCategory.CodeEditor, WritingProfile.Work)
        } +
        listOf("signal", "discord", "telegram", "whatsapp", "messenger", "element").map {
            RecognitionRule(it, null, WritingProfile.Personal)
        } +
        listOf("slack", "microsoft teams", "linear", "notion").map {
            RecognitionRule(it, null, WritingProfile.Work)
        }

private fun compact(value: String) = value.filter(Char::isLetterOrDigit).lowercase()

/** A built-in rule matches a whole word of an identity part, or the whole part once compacted. */
private fun RecognitionRule.matches(identity: List<String>): Boolean {
    val boundary =
        Regex(
            "(^|[^\\p{L}\\p{N}])${Regex.escape(match)}([^\\p{L}\\p{N}]|$)",
            RegexOption.IGNORE_CASE,
        )
    return identity.any { boundary.containsMatchIn(it) || compact(it) == compact(match) }
}

/**
 * The category and profile for an app, then that profile's cleanup and tone. [platformCategory] is
 * what the OS reports, used only when no built-in rule names the app; the desktop has no such
 * signal.
 */
fun resolveRefinementContext(
    applicationId: String,
    applicationName: String,
    platformCategory: AppCategory?,
    textBeforeCaret: String?,
    fallbackProfile: WritingProfile,
    profiles: Map<WritingProfile, WritingProfileSettings>,
): RefinementContext {
    val identity = listOf(applicationId, applicationName)
    val category =
        builtInRules.firstOrNull { it.category != null && it.matches(identity) }?.category
            ?: platformCategory
            ?: if (applicationId.isEmpty() && applicationName.isEmpty()) AppCategory.Unknown
            else AppCategory.General
    val profile =
        builtInRules.firstOrNull { it.profile != null && it.matches(identity) }?.profile
            ?: when (category) {
                AppCategory.Email -> WritingProfile.Email
                AppCategory.AiCoding -> WritingProfile.AiCoding
                AppCategory.CodeEditor,
                AppCategory.Terminal,
                AppCategory.Office -> WritingProfile.Work
                AppCategory.General,
                AppCategory.Browser,
                AppCategory.Unknown -> fallbackProfile
            }
    val settings = profiles[profile] ?: WritingProfileSettings()
    return RefinementContext(
        settings.cleanupStrength,
        settings.tone,
        profile,
        category,
        applicationId,
        applicationName,
        textBeforeCaret,
    )
}
