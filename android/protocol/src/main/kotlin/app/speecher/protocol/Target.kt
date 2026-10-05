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

/** A writing profile, by id: one of the built-ins below, or a custom profile's custom_ id. */
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

enum class CleanupStrength(val id: String) {
    /** Skips refinement entirely, as the desktop does. */
    None("none"),
    LightCleanup("light_cleanup"),
    Balanced("balanced"),
    StrongPolish("strong_polish"),
    /** Only as a custom level's base: none of the level rules, only the level's own. */
    CustomOnly("custom_only"),
}

enum class Tone(val id: String) {
    None("none"),
    Formal("formal"),
    Casual("casual"),
    VeryCasual("very_casual"),
    Excited("excited"),
    GenZ("gen_z"),
}

/** A tone the user defined: the model is told its [name] and follows the [instruction]. */
data class CustomTone(val id: String, val name: String, val instruction: String)

/** A cleanup level the user defined: the rules of its [base] plus the user's [instructions]. */
data class CustomCleanupLevel(
    val id: String,
    val name: String,
    val base: CleanupStrength,
    val instructions: String,
)

/**
 * The id a new custom tone, level or profile named [name] gets: custom_ and the name in lowercase
 * with every other character made _, so it never matches a built-in id, then _2, _3 and so on until
 * it is none of [taken]. The desktop's customChoiceId.
 */
fun customChoiceId(name: String, taken: Collection<String>): String {
    val base =
        "custom_" +
            name.trim().lowercase().map { if (it.isLetterOrDigit()) it else '_' }.joinToString("")
    if (base !in taken) return base
    return generateSequence(2) { it + 1 }.map { "${base}_$it" }.first { it !in taken }
}

/**
 * How one writing profile refines. Every profile defaults to balanced with no tone override. The
 * [instructions] follow the global ones in the prompt. [customCleanupLevel] and [customTone] name a
 * custom level or tone chosen instead of [cleanupStrength] or [tone], which then hold what a
 * deletion falls back to: Medium and no tone override. [name] is a custom profile's; a built-in is
 * called by its label. A non-blank [outputLanguage] has refinement write in that language.
 */
data class WritingProfileSettings(
    val cleanupStrength: CleanupStrength = CleanupStrength.Balanced,
    val tone: Tone = Tone.None,
    val instructions: String = "",
    val customCleanupLevel: String? = null,
    val customTone: String? = null,
    val name: String = "",
    val outputLanguage: String = "",
)

/** The chosen level's id: a built-in's or a custom level's. */
val WritingProfileSettings.cleanupLevelId: String
    get() = customCleanupLevel ?: cleanupStrength.id

/** The chosen tone's id: a built-in's or a custom tone's. */
val WritingProfileSettings.toneId: String
    get() = customTone ?: tone.id

fun WritingProfileSettings.withCleanupLevel(id: String): WritingProfileSettings =
    CleanupStrength.entries
        .firstOrNull { it.id == id }
        ?.let { copy(cleanupStrength = it, customCleanupLevel = null) }
        ?: copy(cleanupStrength = CleanupStrength.Balanced, customCleanupLevel = id)

fun WritingProfileSettings.withTone(id: String): WritingProfileSettings =
    Tone.entries.firstOrNull { it.id == id }?.let { copy(tone = it, customTone = null) }
        ?: copy(tone = Tone.None, customTone = id)

/** Falls back to Medium or no tone override when the custom level or tone chosen is gone. */
fun WritingProfileSettings.withoutDeleted(
    tones: List<CustomTone>,
    levels: List<CustomCleanupLevel>,
): WritingProfileSettings {
    var settings = this
    if (customCleanupLevel != null && levels.none { it.id == customCleanupLevel }) {
        settings = settings.withCleanupLevel(CleanupStrength.Balanced.id)
    }
    if (customTone != null && tones.none { it.id == customTone }) {
        settings = settings.withTone(Tone.None.id)
    }
    return settings
}

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
 * nearby text is excluded (context off or a secure field), which drops the caret keys.
 */
data class RefinementContext(
    val style: CleanupStrength = CleanupStrength.Balanced,
    val tone: Tone = Tone.None,
    val profile: WritingProfile = WritingProfile.Other,
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
    /** The user's instructions from settings: every refinement's, then the profile's. */
    val additionalInstructions: String = "",
    val profileInstructions: String = "",
    /** Replaces the built-in dictation rules when not blank. */
    val customSystemPrompt: String = "",
    /** Set when the tone or level is one the user defined; [style] is then the level's base. */
    val customTone: CustomTone? = null,
    val cleanupLevel: CustomCleanupLevel? = null,
    /** The language code the user dictates in, or Automatic's; anything but English adds a rule. */
    val spokenLanguage: String = ENGLISH_LANGUAGE,
    /** The profile's language to write in; blank keeps the spoken one. */
    val outputLanguage: String = "",
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
 * The category and profile for an app, then that profile's cleanup and tone. The user's [rules] are
 * checked before the built-in ones. [platformCategory] is what the OS reports, used only when no
 * rule names the app; the desktop has no such signal.
 */
fun resolveRefinementContext(
    applicationId: String,
    applicationName: String,
    platformCategory: AppCategory?,
    nearbyText: NearbyText?,
    fallbackProfile: WritingProfile,
    profiles: Map<WritingProfile, WritingProfileSettings>,
    tones: List<CustomTone> = emptyList(),
    levels: List<CustomCleanupLevel> = emptyList(),
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
    val level = levels.firstOrNull { it.id == settings.customCleanupLevel }
    // Translating is refining, so a profile set to None that has an output language refines at
    // Light, as the desktop's refinedCleanupLevel.
    val translatesUnrefined =
        settings.cleanupStrength == CleanupStrength.None && settings.outputLanguage.isNotBlank()
    return RefinementContext(
        level?.base
            ?: if (translatesUnrefined) CleanupStrength.LightCleanup else settings.cleanupStrength,
        settings.tone,
        profile,
        category,
        applicationId,
        applicationName,
        nearbyText,
        profileInstructions = settings.instructions,
        customTone = tones.firstOrNull { it.id == settings.customTone },
        cleanupLevel = level,
        outputLanguage = settings.outputLanguage,
    )
}
