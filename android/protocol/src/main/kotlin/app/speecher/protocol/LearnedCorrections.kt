package app.speecher.protocol

import kotlin.math.max
import kotlin.math.min

// Learned Corrections, ported from src/core/LearnedCorrection.cpp, src/platform/CorrectionDiff.cpp
// and src/core/settings/CorrectionSettingsCodec.cpp.

/**
 * A local Vocabulary Entry inferred from a user's edit shortly after insertion: the words Speecher
 * inserted as [original] and the person's [corrected] spelling, learned in the app with
 * [applicationId], or in every app when that is empty.
 */
data class LearnedCorrection(
    val id: String,
    val original: String,
    val corrected: String,
    val applicationId: String,
    val createdAtMs: Long,
    val confidence: Double,
    val enabled: Boolean = true,
    val evidenceCount: Int = 1,
    val lastObservedAtMs: Long = createdAtMs,
)

/** One observed edit: the words as inserted, as corrected, and how sure the diff is of it. */
data class CorrectionEvidence(val original: String, val corrected: String, val confidence: Double)

/** An edit seen too few times to learn yet. It never leaves the device and is never shown. */
data class PendingCorrection(
    val original: String,
    val corrected: String,
    val applicationId: String,
    val count: Int,
    val firstObservedAtMs: Long,
    val lastObservedAtMs: Long,
    val confidence: Double,
)

/** The learned corrections, newest first, and the edits waiting to be seen again. */
data class Corrections(
    val learned: List<LearnedCorrection> = emptyList(),
    val pending: List<PendingCorrection> = emptyList(),
)

/** How much text on each side of an insertion pins it, the desktop's correctionContextChars. */
const val CORRECTION_CONTEXT_CHARS = 24

/**
 * When the field is first read after an insertion, how long apart later readings are, and when
 * watching stops: the desktop's correctionFirstSampleMs, correctionSettleMs and correctionWindowMs.
 */
const val CORRECTION_FIRST_SAMPLE_MS = 2_000L
const val CORRECTION_SAMPLE_INTERVAL_MS = 2_500L
const val CORRECTION_WINDOW_MS = 30_000L

/** A span longer than this is a paragraph the user rewrote, not a correction of the dictation. */
private const val MAX_EDITED_SPAN_CHARS = 500
/** Neither side of a correction may be longer than this. */
private const val MAX_CORRECTION_CHARS = 64
private const val MAX_CORRECTION_WORDS = 3
/** A fix of case, spaces or hyphens only, which is learned the first time. */
private const val SPELLING_CONFIDENCE = 0.98
private const val EDIT_CONFIDENCE = 0.75
/** Evidence below this is dropped; below the second it must be seen twice to be learned. */
private const val MIN_CONFIDENCE = 0.65
private const val LEARN_AT_ONCE_CONFIDENCE = 0.9

/**
 * The text a dictation inserted, pinned by up to [CORRECTION_CONTEXT_CHARS] characters on each
 * side. Re-finding them locates the same span after the user has edited it. A side with fewer
 * characters reaches the field's start or end, and pins the span there: on a phone most dictation
 * ends a message, so where the desktop needs eight characters on each side, an edge counts too.
 */
data class CorrectionWindow(val original: String, val prefix: String, val suffix: String)

/**
 * What lies between [window]'s pins in [text] now, or null when they no longer locate one span: a
 * pin is gone or repeats, the field is empty, or the span is too long to be a correction.
 */
internal fun editedSpan(text: String, window: CorrectionWindow): String? {
    if (text.isEmpty()) return null
    val (_, prefix, suffix) = window
    val start =
        if (prefix.length < CORRECTION_CONTEXT_CHARS) {
            if (!text.startsWith(prefix)) return null
            prefix.length
        } else {
            // Context that repeats cannot say which occurrence Speecher wrote into.
            val at = text.indexOf(prefix)
            if (at < 0 || text.indexOf(prefix, at + 1) >= 0) return null
            at + prefix.length
        }
    val end =
        if (suffix.length < CORRECTION_CONTEXT_CHARS) {
            if (!text.endsWith(suffix) || text.length - suffix.length < start) return null
            text.length - suffix.length
        } else {
            val at = text.indexOf(suffix, start)
            if (at < 0 || text.indexOf(suffix, at + 1) >= 0) return null
            at
        }
    return text.substring(start, end).takeIf { it.length <= MAX_EDITED_SPAN_CHARS }
}

/**
 * Whether [text], the field's whole text, shows [window]'s words between its pins, untouched: the
 * insertion went into this field. The desktop checks the same before it starts watching.
 */
fun insertionLanded(text: String, window: CorrectionWindow): Boolean =
    editedSpan(text, window) == window.original

/**
 * Turns repeated readings of the edited field into a correction, the desktop's CorrectionTracker.
 * An edit counts once the same text has been read twice, which keeps a half-typed word out; a
 * reading that is not a correction starts the count again. A reading that no longer locates the
 * span ends the observation.
 */
class CorrectionTracker(private val window: CorrectionWindow) {
    var active = window.original.isNotEmpty()
        private set

    private var lastEdited: String? = null

    /** The correction [text], the field's whole text now, settles on, or null for none yet. */
    fun sample(text: String): CorrectionEvidence? {
        if (!active) return null
        val edited = editedSpan(text, window)
        if (edited == null) {
            active = false
            return null
        }
        val evidence =
            if (edited == window.original) null else analyzeCorrection(window.original, edited)
        if (evidence == null || edited != lastEdited) {
            lastEdited = edited.takeIf { evidence != null }
            return null
        }
        active = false
        return evidence
    }
}

/**
 * The words [edited] changed in [inserted], widened to whole words, when the change looks like a
 * correction of how a word was heard rather than a rewrite; null otherwise, and for anything that
 * looks like a secret.
 */
fun analyzeCorrection(inserted: String, edited: String): CorrectionEvidence? {
    if (
        inserted == edited ||
            inserted.isEmpty() ||
            edited.isEmpty() ||
            looksSecret(inserted) ||
            looksSecret(edited)
    )
        return null

    var prefix = 0
    val sharedLength = min(inserted.length, edited.length)
    while (prefix < sharedLength && inserted[prefix] == edited[prefix]) prefix++
    var suffix = 0
    while (
        suffix < inserted.length - prefix &&
            suffix < edited.length - prefix &&
            inserted[inserted.length - suffix - 1] == edited[edited.length - suffix - 1]
    ) suffix++

    while (prefix > 0 && inserted[prefix - 1].isWordPart()) prefix--
    var insertedEnd = inserted.length - suffix
    var editedEnd = edited.length - suffix
    while (
        insertedEnd < inserted.length &&
            editedEnd < edited.length &&
            inserted[insertedEnd].isWordPart() &&
            edited[editedEnd].isWordPart()
    ) {
        insertedEnd++
        editedEnd++
    }

    val original = inserted.substring(prefix, insertedEnd).trim(Char::isQtWhitespace)
    val corrected = edited.substring(prefix, editedEnd).trim(Char::isQtWhitespace)
    if (
        original.isEmpty() ||
            corrected.isEmpty() ||
            original == corrected ||
            original.length > MAX_CORRECTION_CHARS ||
            corrected.length > MAX_CORRECTION_CHARS ||
            looksSecret(original) ||
            looksSecret(corrected)
    )
        return null

    val comparableOriginal = foldCase(original.filter { it.isWordPart() })
    val comparableCorrected = foldCase(corrected.filter { it.isWordPart() })
    if (comparableOriginal.isEmpty() || comparableCorrected.isEmpty()) return null
    if (comparableOriginal == comparableCorrected) {
        fun spelling(text: String) = foldCase(text.filter { !it.isQtWhitespace() && it != '-' })
        return if (spelling(original) == spelling(corrected))
            CorrectionEvidence(original, corrected, SPELLING_CONFIDENCE)
        else null
    }
    // Adding or dropping letters, as in file and files, is grammar, not a misheard word.
    if (
        (comparableOriginal.length < comparableCorrected.length &&
            isSubsequence(comparableOriginal, comparableCorrected)) ||
            (comparableCorrected.length < comparableOriginal.length &&
                isSubsequence(comparableCorrected, comparableOriginal))
    )
        return null

    val originalWords = words(original)
    val correctedWords = words(corrected)
    if (originalWords.size > MAX_CORRECTION_WORDS || correctedWords.size > MAX_CORRECTION_WORDS)
        return null
    if (originalWords.any { it.length > 1 && it in correctedWords }) return null
    if (
        originalWords.size == correctedWords.size &&
            originalWords.size > 1 &&
            originalWords.zip(correctedWords).count { (a, b) -> a != b } > 1
    )
        return null

    val longest = max(comparableOriginal.length, comparableCorrected.length)
    val shortest = min(comparableOriginal.length, comparableCorrected.length)
    if (
        shortest * 3 < longest ||
            editDistance(comparableOriginal, comparableCorrected) > max(3, (longest + 1) / 2)
    )
        return null
    return CorrectionEvidence(original, corrected, EDIT_CONFIDENCE)
}

/**
 * [corrections] with [evidence] from the app [applicationId] counted at [nowMs]. A correction
 * already learned for that app, or for every app, counts it; one learned in another app becomes a
 * correction for every app. Otherwise the edit waits in pending until it has been seen twice, or
 * learns at once when it is only a fix of case, spaces or hyphens. Evidence that contradicts a
 * learned correction, or that is too unsure, changes nothing. [newId] names a new correction.
 */
fun withEvidence(
    corrections: Corrections,
    evidence: CorrectionEvidence,
    applicationId: String,
    nowMs: Long,
    newId: () -> String,
): Corrections {
    val original = evidence.original.trim(Char::isQtWhitespace)
    val corrected = evidence.corrected.trim(Char::isQtWhitespace)
    val application = applicationId.trim(Char::isQtWhitespace)
    if (
        original.isEmpty() ||
            corrected.isEmpty() ||
            application.isEmpty() ||
            original == corrected ||
            evidence.confidence < MIN_CONFIDENCE
    )
        return corrections

    val learned = corrections.learned
    fun seenAgain(index: Int, scope: String) =
        corrections.copy(
            learned =
                learned.toMutableList().apply {
                    this[index] =
                        this[index].let {
                            it.copy(
                                applicationId = scope,
                                evidenceCount = it.evidenceCount + 1,
                                lastObservedAtMs = nowMs,
                                confidence = max(it.confidence, evidence.confidence),
                            )
                        }
                }
        )
    val inScope = learned.indexOfFirst {
        sameTerm(it.original, original) &&
            (sameTerm(it.applicationId, application) || it.applicationId.isEmpty())
    }
    if (inScope >= 0) {
        if (!sameTerm(learned[inScope].corrected, corrected)) return corrections
        return seenAgain(inScope, learned[inScope].applicationId)
    }
    val elsewhere = learned.indexOfFirst {
        sameTerm(it.original, original) &&
            sameTerm(it.corrected, corrected) &&
            !sameTerm(it.applicationId, application)
    }
    if (elsewhere >= 0) return seenAgain(elsewhere, "")

    val pending = corrections.pending.toMutableList()
    var index = pending.indexOfFirst {
        sameTerm(it.original, original) &&
            sameTerm(it.corrected, corrected) &&
            sameTerm(it.applicationId, application)
    }
    if (index < 0) {
        index = pending.indexOfFirst {
            sameTerm(it.original, original) && sameTerm(it.corrected, corrected)
        }
        if (index >= 0) pending[index] = pending[index].copy(applicationId = "")
    }
    if (index < 0) {
        pending +=
            PendingCorrection(
                original,
                corrected,
                application,
                1,
                nowMs,
                nowMs,
                evidence.confidence,
            )
        index = pending.lastIndex
    } else {
        pending[index] =
            pending[index].let {
                it.copy(
                    count = it.count + 1,
                    lastObservedAtMs = nowMs,
                    confidence = max(it.confidence, evidence.confidence),
                )
            }
    }

    val seen = pending[index]
    if (evidence.confidence < LEARN_AT_ONCE_CONFIDENCE && seen.count < 2)
        return Corrections(learned, pending)
    pending.removeAt(index)
    val activated =
        LearnedCorrection(
            newId(),
            seen.original,
            seen.corrected,
            seen.applicationId,
            seen.firstObservedAtMs,
            seen.confidence,
            evidenceCount = seen.count,
            lastObservedAtMs = seen.lastObservedAtMs,
        )
    return Corrections(listOf(activated) + learned, pending)
}

private val wordPattern = Regex("[\\p{L}\\p{N}]+")

private fun words(text: String): List<String> =
    wordPattern.findAll(text).map { foldCase(it.value) }.toList()

private val secretPattern =
    Regex(
        "(?:https?://|[\\w.+-]+@[\\w.-]+\\.[A-Za-z]{2,}|" +
            "(?:password|secret|token|api[ _-]?key)\\s*[:=]|" +
            "\\b\\d{12,19}\\b|\\b[A-Za-z0-9_/-]{20,}={0,2}\\b)",
        RegexOption.IGNORE_CASE,
    )
private val separators = Regex("[\\s-]")
private val cardDigits = Regex("(?<!\\d)\\d{12,19}(?!\\d)")

/** A URL, an email address, a credential assignment, or a long run of digits or token text. */
private fun looksSecret(text: String): Boolean =
    secretPattern.containsMatchIn(text) || cardDigits.containsMatchIn(text.replace(separators, ""))

private fun isSubsequence(shorter: String, longer: String): Boolean {
    var index = 0
    for (character in longer) {
        if (index < shorter.length && shorter[index] == character) index++
    }
    return index == shorter.length
}

/** Levenshtein distance between two case-folded strings. */
private fun editDistance(left: String, right: String): Int {
    var previous = IntArray(right.length + 1) { it }
    var current = IntArray(right.length + 1)
    for (leftIndex in 1..left.length) {
        current[0] = leftIndex
        for (rightIndex in 1..right.length) {
            current[rightIndex] =
                minOf(
                    previous[rightIndex] + 1,
                    current[rightIndex - 1] + 1,
                    previous[rightIndex - 1] +
                        if (left[leftIndex - 1] == right[rightIndex - 1]) 0 else 1,
                )
        }
        previous = current.also { current = previous }
    }
    return previous[right.length]
}
