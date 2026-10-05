package app.speecher.protocol

import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonArray
import kotlinx.serialization.json.JsonElement
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive

// The desktop's BindingProcessor, BindingMatcher and NoBindDirectiveParser, and the replacement
// steps of its TranscriptPipeline.

/** A spoken [phrase] and the exact [text], a word or a multi-line Snippet, put in its place. */
data class Replacement(val phrase: String, val text: String)

/** One word of a text, case-folded, and the span it covers there. */
private data class Word(val text: String, val start: Int, val end: Int)

/** Letters and numbers make words; anything else, punctuation included, separates them. */
private fun Char.isWordPart(): Boolean =
    isLetterOrDigit() ||
        category == CharCategory.LETTER_NUMBER ||
        category == CharCategory.OTHER_NUMBER

private fun words(text: String): List<Word> {
    val words = mutableListOf<Word>()
    var start = -1
    for (index in 0..text.length) {
        val part = index < text.length && text[index].isWordPart()
        if (part && start < 0) start = index
        if (!part && start >= 0) {
            words += Word(foldCase(text.substring(start, index)), start, index)
            start = -1
        }
    }
    return words
}

/** How a phrase matches: its words, case-folded, with single spaces between them. */
fun normalizedPhrase(text: String): String = words(text).joinToString(" ") { it.text }

/**
 * The rules that can apply, in order, with their phrases trimmed, and why each other one cannot, in
 * the desktop's wording. A phrase needs a letter or number, its text must not be blank, and the
 * first of two phrases that normalize alike wins.
 */
private fun check(rules: List<Replacement>): Pair<List<Replacement>, List<String>> {
    val usable = mutableListOf<Replacement>()
    val problems = mutableListOf<String>()
    val seen = mutableMapOf<String, Int>()
    rules.forEachIndexed { index, rule ->
        val row = index + 1
        val phrase = rule.phrase.trim()
        val normalized = normalizedPhrase(phrase)
        val duplicateOf = seen[normalized]
        when {
            normalized.isEmpty() ->
                problems +=
                    "Row $row needs a spoken phrase containing at least one letter or number."
            rule.text.isBlank() -> problems += "Row $row needs replacement text."
            duplicateOf != null ->
                problems +=
                    "Row $row duplicates the normalized spoken phrase from row ${duplicateOf + 1}."
            else -> {
                seen[normalized] = index
                usable += Replacement(phrase, rule.text)
            }
        }
    }
    return usable to problems
}

/** Why [rules] cannot all be saved, one message per rule; empty when they can. */
fun replacementProblems(rules: List<Replacement>): List<String> = check(rules).second

/**
 * The spoken phrases refinement gets as binding_aliases, so it can repair a misheard one for the
 * replacement after it: each phrase as written and normalized, once each ignoring case.
 */
fun replacementAliases(rules: List<Replacement>): List<String> =
    check(rules)
        .first
        .flatMap { listOf(it.phrase, normalizedPhrase(it.phrase)) }
        .map { it.trim().replace(WHITESPACE, " ") }
        .filter(String::isNotEmpty)
        .distinctBy(::foldCase)

// No (?U): Android's ICU regex rejects it, and its \s already covers Unicode spaces.
private val WHITESPACE = Regex("\\s+")

/** {date} and {time} in each rule's text become [date] and [time]; other braces stay as written. */
fun withVariablesFilled(rules: List<Replacement>, date: String, time: String): List<Replacement> =
    rules.map {
        it.copy(text = it.text.replace("{date}", date).replace("{time}", time))
    }

/**
 * [current] with the snippets of a JSON file after them: an array of objects with phrase or trigger
 * and replacement, expansion or text, an object holding such an array as snippets, or an object
 * mapping phrases to text. Throws [IllegalArgumentException] saying why the file cannot be
 * imported, the merged list's row numbers included.
 */
fun importReplacements(current: List<Replacement>, json: String): List<Replacement> {
    val merged = current + parseReplacementsJson(json)
    val problems = replacementProblems(merged)
    require(problems.isEmpty()) { problems.joinToString("\n") }
    return merged
}

private fun parseReplacementsJson(json: String): List<Replacement> {
    val document =
        try {
            // A byte order mark, as Windows editors write, is not JSON; the desktop skips it.
            Json.parseToJsonElement(json.removePrefix("﻿"))
        } catch (_: SerializationException) {
            throw IllegalArgumentException("Invalid JSON.")
        }
    fun JsonElement?.string(): String? = (this as? JsonPrimitive)?.takeIf { it.isString }?.content
    fun fromArray(array: JsonArray) = array.map {
        val item = it as? JsonObject ?: JsonObject(emptyMap())
        Replacement(
            item["phrase"].string() ?: item["trigger"].string().orEmpty(),
            item["replacement"].string()
                ?: item["expansion"].string()
                ?: item["text"].string().orEmpty(),
        )
    }
    val rules =
        when {
            document is JsonArray -> fromArray(document)
            document is JsonObject && document["snippets"] is JsonArray ->
                fromArray(document["snippets"] as JsonArray)
            // In key order, as the desktop's QJsonObject reads them.
            document is JsonObject ->
                document.entries
                    .sortedBy { it.key }
                    .mapNotNull { (phrase, text) -> text.string()?.let { Replacement(phrase, it) } }
            else -> throw IllegalArgumentException("Snippet JSON must be an array or object.")
        }
    val (usable, problems) = check(rules)
    require(problems.isEmpty()) { problems.joinToString("\n") }
    return usable
}

private class Match(
    val rule: Replacement,
    val startWord: Int,
    val wordCount: Int,
    val start: Int,
    val end: Int,
) {
    val endWord: Int
        get() = startWord + wordCount
}

/**
 * Where [rules] match [text], none overlapping: longer phrases first, then the earlier one, then
 * the earlier rule. In text order.
 */
private fun findMatches(text: String, rules: List<Replacement>): List<Match> {
    val words = words(text)
    val candidates =
        check(rules)
            .first
            .flatMap { rule ->
                val phrase = normalizedPhrase(rule.phrase).split(' ')
                (0..words.size - phrase.size)
                    .filter { start -> phrase.indices.all { words[start + it].text == phrase[it] } }
                    .map { start ->
                        val end = words[start + phrase.size - 1].end
                        Match(rule, start, phrase.size, words[start].start, end)
                    }
            }
            // Stable, so of two equal candidates the earlier rule's stays first.
            .sortedWith(compareByDescending<Match> { it.wordCount }.thenBy { it.startWord })
    val taken = BooleanArray(words.size)
    val accepted = mutableListOf<Match>()
    for (candidate in candidates) {
        val span = candidate.startWord until candidate.endWord
        if (span.any { taken[it] }) continue
        span.forEach { taken[it] = true }
        accepted += candidate
    }
    return accepted.sortedBy { it.start }
}

private fun replaceMatches(text: String, matches: List<Match>, by: (Int, Match) -> String) =
    buildString {
        var cursor = 0
        matches.forEachIndexed { index, match ->
            append(text, cursor, match.start)
            append(by(index, match))
            cursor = match.end
        }
        append(text, cursor, text.length)
    }

private fun placeholder(index: Int) = "SPEECHER_BINDING_$index"

/** A placeholder the model kept exactly, not part of a longer identifier. */
private val EXACT_PLACEHOLDER = Regex("(?<![A-Za-z0-9_])SPEECHER_BINDING_[0-9]+(?![A-Za-z0-9_])")

private val PLACEHOLDER = Regex("SPEECHER_BINDING_[0-9]+")

private val INDEX_WORDS =
    setOf("zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine")

/** What a dictation inserts once [replaceSpoken] has run, and what refinement gets instead. */
class ReplacedTranscript
internal constructor(
    /** What Insert commits: each matched phrase replaced by its text. */
    val text: String,
    /** What refinement gets: each match as a placeholder the model is told to keep. */
    val refinementInput: String,
    /** Every word was a phrase, so a model has nothing to clean up. */
    val skipsRefinement: Boolean,
    private val placeholders: Map<String, String>,
    /** The rules left once a no-bind directive excluded some, matched again after refinement. */
    private val rules: List<Replacement>,
    private val replacesAfterRefinement: Boolean,
) {
    /**
     * The refined text with phrases the model repaired replaced and the placeholders restored; null
     * when it is empty or lost, changed or invented a placeholder, so the caller inserts [text].
     */
    fun restore(refined: String): String? {
        val trimmed = refined.trim()
        if (trimmed.isEmpty()) return null
        return restorePlaceholders(
            if (replacesAfterRefinement) replaceOutsidePlaceholders(trimmed) else trimmed
        )
    }

    /**
     * [restore] for a revised selection. Its words are the document's, not the dictation's, so no
     * phrase in them is replaced, as on the desktop; only the placeholders are restored.
     */
    fun restoreEdit(revised: String): String? =
        revised.trim().takeIf(String::isNotEmpty)?.let(::restorePlaceholders)

    /**
     * The refined text streamed so far, for the panel: complete placeholders show their text and
     * one still arriving at the end is hidden until it is complete.
     */
    fun preview(streamed: String): String {
        val tail = streamed.lastIndexOf("SPEECHER")
        val shown =
            if (tail < 0) streamed
            else {
                val token = PLACEHOLDER.matchAt(streamed, tail)
                if (token == null || token.range.last + 1 == streamed.length)
                    streamed.substring(0, tail)
                else streamed
            }
        return PLACEHOLDER.replace(shown) { placeholders[it.value] ?: it.value }
    }

    private fun replaceOutsidePlaceholders(refined: String): String {
        if (rules.isEmpty()) return refined
        fun replace(part: String) =
            findMatches(part, rules).let { matches ->
                replaceMatches(part, matches) { _, m -> m.rule.text }
            }
        return buildString {
            var cursor = 0
            EXACT_PLACEHOLDER.findAll(refined).forEach {
                append(replace(refined.substring(cursor, it.range.first)))
                append(it.value)
                cursor = it.range.last + 1
            }
            append(replace(refined.substring(cursor)))
        }
    }

    private fun restorePlaceholders(refined: String): String? {
        if (placeholders.isEmpty()) return refined
        val restored = StringBuilder()
        val residue = StringBuilder()
        var cursor = 0
        for (token in EXACT_PLACEHOLDER.findAll(refined)) {
            val replacement = placeholders[token.value] ?: return null
            restored.append(refined, cursor, token.range.first).append(replacement)
            residue.append(refined, cursor, token.range.first)
            cursor = token.range.last + 1
        }
        restored.append(refined, cursor, refined.length)
        residue.append(refined, cursor, refined.length)
        // A placeholder the model reworded, such as "speecher binding two", is a lost snippet.
        if ("SPEECHER_BINDING" in residue) return null
        val left = words(residue.toString()).map { it.text }
        val reworded =
            left.indices.drop(2).any {
                left[it - 2] == "speecher" &&
                    left[it - 1] == "binding" &&
                    (left[it].toIntOrNull() != null || left[it] in INDEX_WORDS)
            }
        return if (reworded) null else restored.toString()
    }
}

/**
 * The replacement step of a dictation: [rules] applied to the [transcript] as heard. A spoken
 * "don't replace" directive keeps the phrase it names literal, here and after refinement.
 */
fun replaceSpoken(transcript: String, rules: List<Replacement>): ReplacedTranscript {
    val directives = noBindDirectives(transcript)
    val excluded = excludedPhrases(transcript, directives, rules)
    val active = rules.filter { normalizedPhrase(it.phrase) !in excluded }
    val matches = findMatches(transcript, active)
    return ReplacedTranscript(
        text = replaceMatches(transcript, matches) { _, match -> match.rule.text },
        refinementInput = replaceMatches(transcript, matches) { index, _ -> placeholder(index) },
        skipsRefinement = matches.isNotEmpty() && coversEveryWord(transcript, matches),
        placeholders =
            matches.withIndex().associate { (index, match) ->
                placeholder(index) to match.rule.text
            },
        rules = active,
        // A directive that named no phrase leaves the model to keep it literal, so nothing the
        // model wrote is replaced.
        replacesAfterRefinement = directives.isEmpty() || excluded.isNotEmpty(),
    )
}

private fun coversEveryWord(text: String, matches: List<Match>): Boolean =
    text.indices.all { index ->
        !text[index].isWordPart() || matches.any { index >= it.start && index < it.end }
    }

/** A spoken "don't replace" from its negation to its action word; its target may follow it. */
private class Directive(val startWord: Int, val endWord: Int, val targetFollows: Boolean)

private val ACTION_WORDS =
    setOf("bind", "binding", "bindings", "replace", "replaces", "replacing", "replacement")

private val ANAPHORA = setOf("that", "this", "it")

/** "not", "never", "dont", "don t" or "do not" at [index], as its word count. */
private fun negationLength(words: List<String>, index: Int): Int? =
    when {
        words[index] in setOf("not", "never", "dont") -> 1
        words[index] == "don" && words.getOrNull(index + 1) == "t" -> 2
        words[index] == "do" && words.getOrNull(index + 1) == "not" -> 2
        else -> null
    }

/** The action word within twelve words of [start]: an action, or "turn ... into ... binding". */
private fun actionWord(words: List<String>, start: Int): Int? {
    for (index in start until minOf(words.size, start + 12)) {
        if (words[index] in ACTION_WORDS) return index
        if (words[index] != "turn") continue
        val into =
            (index + 1 until minOf(words.size, index + 8)).firstOrNull { words[it] == "into" }
                ?: continue
        (into + 1 until minOf(words.size, into + 5))
            .firstOrNull { words[it] == "binding" || words[it] == "bindings" }
            ?.let {
                return it
            }
    }
    return null
}

private fun noBindDirectives(text: String): List<Directive> {
    val words = words(text).map { it.text }
    return words.indices.mapNotNull { index ->
        val negation = negationLength(words, index) ?: return@mapNotNull null
        val action = actionWord(words, index + negation) ?: return@mapNotNull null
        val anaphora =
            (index + negation..minOf(words.size - 1, action + 1)).any { words[it] in ANAPHORA }
        Directive(index, action + 1, action + 1 < words.size && !anaphora)
    }
}

/**
 * The normalized phrases [directives] keep literal: a phrase inside one, else the nearest within
 * eight words after it when its target follows, or before it when it points back.
 */
private fun excludedPhrases(
    transcript: String,
    directives: List<Directive>,
    rules: List<Replacement>,
): Set<String> {
    if (directives.isEmpty()) return emptySet()
    val matches = findMatches(transcript, rules)
    return directives
        .flatMap { directive ->
            val inside = matches.filter {
                it.startWord >= directive.startWord && it.endWord <= directive.endWord
            }
            inside.ifEmpty {
                listOfNotNull(
                    if (directive.targetFollows)
                        matches
                            .filter { it.startWord >= directive.endWord }
                            .filter { it.startWord - directive.endWord <= 8 }
                            .minByOrNull { it.startWord - directive.endWord }
                    else
                        matches
                            .filter { it.endWord <= directive.startWord }
                            .filter { directive.startWord - it.endWord <= 8 }
                            .minByOrNull { directive.startWord - it.endWord }
                )
            }
        }
        .map { normalizedPhrase(it.rule.phrase) }
        .toSet()
}
