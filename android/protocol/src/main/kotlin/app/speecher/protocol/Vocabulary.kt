package app.speecher.protocol

import java.text.BreakIterator

// Vocabulary words, ported from src/core/Vocabulary.cpp and src/core/VocabularyLimit.cpp.

/**
 * A vocabulary word: the [term] speech hints and refinement spell, what it means and when it
 * applies for refinement, and the profiles it is limited to. No profiles means every profile. A
 * [keyTerm] goes to the speech service as a hint, and [priority] puts it first in line there;
 * refinement uses every word either way. [source] says where it came from (manual, csv, learned or
 * legacy), and [frequency] and [lastUsedMs] count the inserted dictations that contained it.
 */
data class VocabularyWord(
    val term: String,
    val context: String = "",
    val profiles: Set<WritingProfile> = emptySet(),
    val keyTerm: Boolean = true,
    val priority: Boolean = false,
    val source: String = "manual",
    val frequency: Int = 0,
    val lastUsedMs: Long = 0,
)

/** How many terms a speech request carries, and how many whitespace-separated tokens in all. */
const val MAX_SPEECH_TERMS = 100
const val MAX_SPEECH_TOKENS = 500

/**
 * [words] trimmed, one per term ignoring case, in the order the speech service is cut from:
 * priority key terms, then the most used, then the most recently used, then by term. The first copy
 * of a term keeps its key term, context and profiles, taking only a context it lacks from a later
 * one, so an imported row cannot change a word already listed. Every word is kept; the limits apply
 * where the terms are sent. The desktop's normalizeVocabularyEntries.
 */
fun normalizedVocabulary(words: List<VocabularyWord>): List<VocabularyWord> {
    val kept = mutableListOf<VocabularyWord>()
    for (raw in words) {
        val word =
            raw.copy(
                term = raw.term.simplified(),
                context = raw.context.trim(),
                source = raw.source.simplified().ifEmpty { "manual" },
                frequency = raw.frequency.coerceAtLeast(0),
                lastUsedMs = raw.lastUsedMs.coerceAtLeast(0),
            )
        if (word.term.isEmpty()) continue
        val index = kept.indexOfFirst { sameTerm(it.term, word.term) }
        if (index < 0) {
            kept += word
            continue
        }
        val first = kept[index]
        kept[index] =
            first.copy(
                priority = first.priority || word.priority,
                frequency = maxOf(first.frequency, word.frequency),
                lastUsedMs = maxOf(first.lastUsedMs, word.lastUsedMs),
                context = first.context.ifEmpty { word.context },
            )
    }
    // Priority only counts for a key term.
    return kept.sortedWith(
        compareBy<VocabularyWord> { !(it.priority && it.keyTerm) }
            .thenByDescending { it.frequency }
            .thenByDescending { it.lastUsedMs }
            .thenBy { foldCase(it.term) }
    )
}

/**
 * The key terms among [words] a speech request carries, in order: no more than [MAX_SPEECH_TERMS]
 * terms of [MAX_SPEECH_TOKENS] tokens in all. A term that does not fit is skipped and shorter ones
 * after it can still go. The desktop's speechVocabulary and VocabularyLimit::limited.
 */
fun speechTerms(words: List<VocabularyWord>): List<String> =
    limitedSpeechTerms(normalizedVocabulary(words).filter { it.keyTerm }.map { it.term })

/**
 * The speech terms of a dictation under [profile]: [vocabulary]'s key terms for it, then each
 * enabled correction's spelling. Corrections sit last, so an over-cap list drops them before any
 * term the person typed. One whose spelling is a listed term adds nothing: that word's own profiles
 * and key term decide whether it goes. The desktop's speechVocabulary.
 */
fun speechTerms(
    vocabulary: List<VocabularyWord>,
    profile: WritingProfile,
    corrections: List<LearnedCorrection>,
): List<String> {
    val words = normalizedVocabulary(vocabulary)
    val terms =
        words.filter { it.keyTerm && it.appliesTo(profile) }.mapTo(mutableListOf()) { it.term }
    for (correction in corrections) {
        val spelling = correction.corrected.simplified()
        if (
            correction.enabled &&
                words.none { sameTerm(it.term, spelling) } &&
                terms.none { sameTerm(it, spelling) }
        )
            terms += spelling
    }
    return limitedSpeechTerms(terms)
}

/**
 * The words a refinement under [profile] reads: each enabled correction's spelling first, as there
 * are few and each came from a real edit, then [vocabulary]'s words for the profile. A spelling
 * appears once, ignoring case, and keeps the context a word gives it. A correction whose spelling
 * is a word limited to other profiles stays out with it. The desktop's refinementVocabulary.
 */
fun refinementVocabulary(
    vocabulary: List<VocabularyWord>,
    profile: WritingProfile,
    corrections: List<LearnedCorrection>,
): List<VocabularyWord> {
    val words = normalizedVocabulary(vocabulary)
    val learned =
        corrections
            .map { it.copy(corrected = it.corrected.simplified()) }
            .filter { correction ->
                correction.enabled &&
                    words.none { sameTerm(it.term, correction.corrected) && !it.appliesTo(profile) }
            }
            .map { VocabularyWord(it.corrected, source = "learned") }
    val kept = mutableListOf<VocabularyWord>()
    for (word in learned + words.filter { it.appliesTo(profile) }) {
        val index = kept.indexOfFirst { sameTerm(it.term, word.term) }
        if (index < 0) kept += word
        else if (kept[index].context.isEmpty())
            kept[index] = kept[index].copy(context = word.context)
    }
    return kept
}

/**
 * Whether a dictation under [profile] uses this word: it is limited to no profile or to that one.
 */
private fun VocabularyWord.appliesTo(profile: WritingProfile): Boolean =
    profiles.isEmpty() || profile in profiles

/**
 * [terms] in order, no more than [MAX_SPEECH_TERMS] of [MAX_SPEECH_TOKENS] tokens in all. A term
 * that does not fit is skipped and shorter ones after it can still go. VocabularyLimit::limited.
 */
private fun limitedSpeechTerms(terms: List<String>): List<String> {
    val kept = mutableListOf<String>()
    var tokens = 0
    for (term in terms) {
        val termTokens = term.split(' ').size
        if (kept.size >= MAX_SPEECH_TERMS || tokens + termTokens > MAX_SPEECH_TOKENS) continue
        kept += term
        tokens += termTokens
    }
    return kept
}

/**
 * [words] with one more use at [nowMs] for each whose term [text] contains as whole words, ignoring
 * case and how much whitespace separates them. The desktop's recordUsage.
 */
fun withUsage(words: List<VocabularyWord>, text: String, nowMs: Long): List<VocabularyWord> {
    val boundaries = BreakIterator.getWordInstance().apply { setText(text) }
    return words.map {
        if (containsTerm(text, boundaries, it.term))
            it.copy(frequency = it.frequency + 1, lastUsedMs = nowMs)
        else it
    }
}

/** Whether [a] and [b] are one term, compared ignoring case as the desktop does. */
fun sameTerm(a: String, b: String): Boolean = foldCase(a) == foldCase(b)

private fun containsTerm(text: String, boundaries: BreakIterator, term: String): Boolean {
    val words = term.simplified().split(' ').filter(String::isNotEmpty)
    if (words.isEmpty()) return false
    val expression =
        Regex(words.joinToString("\\s+", transform = Regex::escape), RegexOption.IGNORE_CASE)
    return expression.findAll(text).any {
        boundaries.isBoundary(it.range.first) && boundaries.isBoundary(it.range.last + 1)
    }
}

/**
 * The words in a CSV file, as the desktop's parseVocabularyCsv reads it. A header row naming a term
 * column picks the columns by name (term, source, starred, frequency, last_used_ms or last_used,
 * context); without one they come in that order. A row with no source is "csv".
 *
 * @throws IllegalArgumentException for a quoted field that never ends, with the desktop's message.
 */
fun parseVocabularyCsv(csv: String): List<VocabularyWord> {
    val rows = csvRows(csv.removePrefix("\uFEFF"))
    if (rows.isEmpty()) return emptyList()
    val header = rows.first().map { foldCase(it.trim()).replace(' ', '_') }
    val hasHeader = "term" in header
    fun column(name: String, position: Int) = if (hasHeader) header.indexOf(name) else position
    val term = column("term", 0)
    val source = column("source", 1)
    val starred = column("starred", 2)
    val frequency = column("frequency", 3)
    val lastUsed = column("last_used_ms", 4).takeIf { it >= 0 } ?: column("last_used", 4)
    val context = column("context", 5)
    return normalizedVocabulary(
        rows.drop(if (hasHeader) 1 else 0).map { row ->
            fun field(index: Int) = row.getOrNull(index)?.trim().orEmpty()
            VocabularyWord(
                field(term),
                context = field(context),
                priority = foldCase(field(starred)) in setOf("true", "yes", "1", "starred"),
                source = field(source).ifEmpty { "csv" },
                frequency = field(frequency).toIntOrNull() ?: 0,
                lastUsedMs = field(lastUsed).toLongOrNull() ?: 0,
            )
        }
    )
}

/** RFC 4180 rows: quoted fields with doubled quotes, CRLF or LF line ends, blank rows dropped. */
private fun csvRows(text: String): List<List<String>> {
    val rows = mutableListOf<List<String>>()
    var row = mutableListOf<String>()
    val field = StringBuilder()
    fun endField() {
        row += field.toString()
        field.clear()
    }
    fun endRow() {
        endField()
        if (row.joinToString("").isNotBlank()) rows += row
        row = mutableListOf()
    }
    var quoted = false
    var index = 0
    while (index < text.length) {
        val c = text[index]
        when {
            quoted && c == '"' && text.getOrNull(index + 1) == '"' -> field.append(text[index++])
            quoted && c == '"' -> quoted = false
            quoted -> field.append(c)
            c == '"' && field.isEmpty() -> quoted = true
            c == ',' -> endField()
            c == '\n' -> endRow()
            c != '\r' -> field.append(c)
        }
        index++
    }
    require(!quoted) { "CSV contains an unterminated quoted field." }
    endRow()
    return rows
}
