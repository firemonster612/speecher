package app.speecher.protocol

import kotlinx.serialization.Serializable

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

/** A writing profile, by id: one of the built-ins below, or a custom profile's custom_ id. */
@Serializable
@JvmInline
value class WritingProfile(val id: String) {
    val isBuiltIn: Boolean
        get() = this in entries

    companion object {
        val Work = WritingProfile("work")
        val Email = WritingProfile("email")
        val Personal = WritingProfile("personal")
        val AiCoding = WritingProfile("ai_coding")
        val Other = WritingProfile("other")

        /** The built-ins, in the desktop's order. */
        val entries = listOf(Work, Email, Personal, AiCoding, Other)
    }
}

/** The built-in profiles' labels, from the desktop's Target.cpp, in its order. */
val builtInProfileLabels =
    mapOf(
        WritingProfile.Work to "Work",
        WritingProfile.Email to "Email",
        WritingProfile.Personal to "Personal",
        WritingProfile.AiCoding to "AI coding",
        WritingProfile.Other to "Other",
    )

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

/**
 * A rule the user wrote. Refinement is told [instruction], or [name] when the instruction is blank.
 * The [name] is required and unique among the user's rules. [everyProfile] has it on unless a
 * profile turns it off; otherwise it is off unless one turns it on.
 */
@Serializable
data class RefinementRule(
    val id: String,
    val name: String,
    val instruction: String = "",
    val everyProfile: Boolean = false,
)

/**
 * The id a new custom profile named [name] gets: custom_ and the name in lowercase with every other
 * character made _, so it never matches a built-in id, then _2, _3 and so on until it is none of
 * [taken]. The desktop's customChoiceId.
 */
fun customChoiceId(name: String, taken: Collection<String>): String {
    val base =
        "custom_" +
            name.trim().lowercase().map { if (it.isLetterOrDigit()) it else '_' }.joinToString("")
    if (base !in taken) return base
    return generateSequence(2) { it + 1 }.map { "${base}_$it" }.first { it !in taken }
}

/**
 * How one writing profile refines. Every profile defaults to balanced with no tone override.
 * [rules] are the rules this profile switched away from their default, by rule id, built-in rules
 * and the user's alike; an entry equal to the default is never kept (see [withRule]). [name] is a
 * custom profile's; a built-in is called by its label. A non-blank [outputLanguage] has refinement
 * write in that language.
 */
data class WritingProfileSettings(
    val cleanupStrength: CleanupStrength = CleanupStrength.Balanced,
    val tone: Tone = Tone.None,
    val rules: Map<String, Boolean> = emptyMap(),
    val name: String = "",
    val outputLanguage: String = "",
)

/**
 * The field's text around the caret: [before] the selection start, [after] the selection end.
 * Selection offsets are absolute in the field, -1 when the editor does not report them, as on the
 * desktop.
 */
data class NearbyText(
    val before: String = "",
    val after: String = "",
    val selectionStart: Int = -1,
    val selectionEnd: Int = -1,
)

/**
 * The refinement configuration and target context sent to the refiner. [nearbyText] is null when
 * nearby text is excluded (context off or a secure field) or could not be read, which drops its
 * keys, so an empty [NearbyText.before] means the caret is at the start of the field.
 */
data class RefinementContext(
    val style: CleanupStrength = CleanupStrength.Balanced,
    val tone: Tone = Tone.None,
    val profile: WritingProfile = WritingProfile.Other,
    /** What the prompt calls [profile]: a built-in's label or a custom profile's name. */
    val profileName: String = "",
    val category: AppCategory = AppCategory.Unknown,
    val applicationId: String = "",
    val applicationName: String = "",
    val nearbyText: NearbyText? = null,
    /** The field's input type, sent as the desktop's control_role. */
    val controlRole: String = "",
    /** The field's placeholder text. The desktop has no such key; sent only when present. */
    val fieldHint: String = "",
    val windowTitle: String = "",
    /** The target window's visible text. The desktop has no such key; sent only when present. */
    val screenText: String = "",
    /** A base64 JPEG of the screen, attached as an image for a vision model. */
    val screenshotJpeg: String? = null,
    /** The text of each of the user's rules that is on, in their order. */
    val userRules: List<String> = emptyList(),
    /**
     * The prompt sentence of each built-in rule the profile switched away from its level's default,
     * in the rules' order.
     */
    val changedSteps: List<String> = emptyList(),
    /** The text is a prompt for a coding agent: the [CODING_AGENT_RULE]. */
    val codingAgent: Boolean = false,
    /** Replaces the built-in dictation rules when not blank. */
    val customSystemPrompt: String = "",
    /** The language code the user dictates in, or Automatic's; anything but English adds a rule. */
    val spokenLanguage: String = ENGLISH_LANGUAGE,
    /** The profile's language to write in; blank keeps the spoken one. */
    val outputLanguage: String = "",
    /** The replacements' spoken phrases, sent as binding_aliases; see [replacementAliases]. */
    val bindingAliases: List<String> = emptyList(),
    /**
     * The field's selected text, which the dictation's words say how to edit; null for an ordinary
     * dictation. Set, the refiner returns the revised selection.
     */
    val selectedText: String? = null,
)

/** An app a rule recognises by [match], and the app type and profile it gets. */
data class RecognitionRule(
    val match: String,
    val category: AppCategory?,
    val profile: WritingProfile?,
)

/** The desktop's built-in rules, in its order. */
val builtInRules: List<RecognitionRule> =
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

/**
 * A built-in rule matches a whole word of an identity part, or the whole part once compacted. A
 * custom rule matches anywhere in the identity, compacted or not, as the desktop's ruleMatches.
 */
private fun RecognitionRule.matches(identity: List<String>, builtIn: Boolean = true): Boolean {
    if (compact(match).isEmpty()) return false
    if (!builtIn) {
        val joined = identity.joinToString(" ")
        return joined.contains(match, ignoreCase = true) || compact(joined).contains(compact(match))
    }
    val boundary =
        Regex(
            "(^|[^\\p{L}\\p{N}])${Regex.escape(match)}([^\\p{L}\\p{N}]|$)",
            RegexOption.IGNORE_CASE,
        )
    return identity.any { boundary.containsMatchIn(it) || compact(it) == compact(match) }
}

/**
 * The category and profile for an app, then that profile's cleanup, tone and rules. The user's app
 * [rules] are checked before the built-in ones. [platformCategory] is what the OS reports, used
 * only when no rule names the app; the desktop has no such signal.
 */
fun resolveRefinementContext(
    applicationId: String,
    applicationName: String,
    platformCategory: AppCategory?,
    nearbyText: NearbyText?,
    fallbackProfile: WritingProfile,
    profiles: Map<WritingProfile, WritingProfileSettings>,
    refinementRules: List<RefinementRule> = emptyList(),
    rules: List<RecognitionRule> = emptyList(),
): RefinementContext {
    // The desktop also matches the process name, the accessibility role and, outside AI coding
    // rules, the window title. Android has no process name apart from the package, the role here is
    // an input type rather than an app's own role, and the window title is only read when the user
    // opts into screen text, so apps are recognised by id and name alone.
    val identity = listOf(applicationId, applicationName)
    val category =
        rules.firstOrNull { it.category != null && it.matches(identity, builtIn = false) }?.category
            ?: builtInRules.firstOrNull { it.category != null && it.matches(identity) }?.category
            ?: platformCategory
            ?: if (applicationId.isEmpty() && applicationName.isEmpty()) AppCategory.Unknown
            else AppCategory.General
    val profile =
        rules.firstOrNull { it.profile != null && it.matches(identity, builtIn = false) }?.profile
            ?: builtInRules.firstOrNull { it.profile != null && it.matches(identity) }?.profile
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
    // Translating is refining, so a profile set to None that has an output language refines at
    // Light, as the desktop's refinedCleanupLevel.
    val style =
        if (
            settings.cleanupStrength == CleanupStrength.None && settings.outputLanguage.isNotBlank()
        )
            CleanupStrength.LightCleanup
        else settings.cleanupStrength
    // The desktop's fillRules: the rules on for this profile at the level refinement runs at.
    fun on(ruleId: String) = ruleOn(ruleId, profile, settings, style, refinementRules)
    return RefinementContext(
        style,
        settings.tone,
        profile,
        builtInProfileLabels[profile] ?: settings.name,
        category,
        applicationId,
        applicationName,
        nearbyText,
        userRules =
            refinementRules
                .filter { on(it.id) }
                .map { it.instruction.trim().ifEmpty { it.name.trim() } },
        changedSteps =
            builtInRefinementRules.mapNotNull { rule ->
                val on = on(rule.id)
                (if (on) rule.onSentence else rule.offSentence).takeIf {
                    on != ruleDefault(rule.id, profile, style, refinementRules) && it.isNotEmpty()
                }
            },
        codingAgent = on(CODING_AGENT_RULE),
        outputLanguage = settings.outputLanguage,
    )
}
