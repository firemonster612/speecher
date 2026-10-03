package app.speecher.protocol

import kotlinx.serialization.SerializationException
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.JsonPrimitive

private const val MAX_KEYTERMS_BYTES = 1024
private const val MAX_ERROR_FIELD_CHARS = 240
private val ERROR_SUMMARY_KEYS = listOf("type", "code", "error_code", "message", "description")

/** A server message on the Claude voice stream, reduced to what the client acts on. */
sealed interface ClaudeVoiceEvent {
    /** An interim or partial transcript (`TranscriptInterim` or `TranscriptText`). */
    data class Working(val text: String) : ClaudeVoiceEvent

    /** The end of an utterance (`TranscriptEndpoint`); [text] may be empty. */
    data class Endpoint(val text: String) : ClaudeVoiceEvent

    /** The speech-to-text backend failed (`TranscriptError`). */
    data class TranscriptError(val summary: String) : ClaudeVoiceEvent

    /** Any other message of type `error` or carrying an `error` field. */
    data class ServerError(val summary: String) : ClaudeVoiceEvent

    /** Anything else, including text that is not a JSON object. */
    data object Unknown : ClaudeVoiceEvent
}

/** The spoken languages Claude Voice listens for, as codes, Automatic first. */
val claudeVoiceLanguages: List<String> =
    listOf(AUTOMATIC_LANGUAGE) +
        ("am ar be bg bn bs ca cs da de el en es et fa fi fr he hi hr hu id it ja kn ko lt lv mk " +
                "mr ms nl no pl pt ro ru sk sl sr sv sw ta te th tl tr uk ur vi yue zh")
            .split(' ')

/**
 * Query parameters for the stream URL, in the order they are sent. Automatic [language] sends no
 * `language` item, so the service detects it.
 *
 * `forward_interims=typed` is on by default. `SPEECHER_CLAUDE_FORWARD_INTERIMS_TYPED` can turn it
 * off, unless `CLAUDE_CODE_VOICE_FORWARD_INTERIMS_TYPED` forces it on.
 */
fun claudeVoiceStreamQuery(
    language: String,
    env: (String) -> String? = System::getenv,
): List<Pair<String, String>> = buildList {
    add("encoding" to "linear16")
    add("sample_rate" to "16000")
    add("channels" to "1")
    add("endpointing_ms" to "300")
    add("utterance_end_ms" to "1000")
    if (language != AUTOMATIC_LANGUAGE) add("language" to language)
    add("use_conversation_engine" to "true")
    if (typedInterimsEnabled(env)) add("forward_interims" to "typed")
    add("stt_provider" to "deepgram-nova3")
}

/**
 * The `x-config-keyterms` header value: whitespace-collapsed terms joined by commas, first spelling
 * wins among case-insensitive duplicates. Terms that would push the header past 1024 bytes of
 * UTF-8, the encoding the server decodes it with, are dropped; shorter terms after them can still
 * fit.
 */
fun claudeVoiceKeytermsHeader(vocabulary: Iterable<String>): String =
    claudeVoiceKeyterms(vocabulary).joinToString(",")

/** The terms [claudeVoiceKeytermsHeader] carries, as they appear in it. */
fun claudeVoiceKeyterms(vocabulary: Iterable<String>): List<String> =
    vocabulary.toList().let { words ->
        claudeVoiceKeytermIndices(words).map { words[it].simplified() }
    }

/** Which of [vocabulary]'s words the header carries, by position. */
fun claudeVoiceKeytermIndices(vocabulary: List<String>): List<Int> {
    val kept = mutableListOf<Int>()
    var bytes = 0
    val seen = mutableSetOf<String>()
    vocabulary.forEachIndexed { index, value ->
        val term = value.simplified()
        val key = term.lowercaseAscii()
        if (term.isEmpty() || key in seen) return@forEachIndexed
        val separator = if (kept.isEmpty()) 0 else 1
        val size = term.toByteArray(Charsets.UTF_8).size
        if (bytes + separator + size > MAX_KEYTERMS_BYTES) return@forEachIndexed
        seen += key
        kept += index
        bytes += separator + size
    }
    return kept
}

/**
 * Classifies one text frame from the stream. [ClaudeVoiceEvent.Working] and `Endpoint` text is
 * passed through untrimmed.
 */
fun parseClaudeVoiceEvent(message: String): ClaudeVoiceEvent {
    val json =
        try {
            Json.parseToJsonElement(message.removePrefix("\uFEFF")) as? JsonObject
        } catch (e: SerializationException) {
            null
        }
            ?: return ClaudeVoiceEvent
                .Unknown // Qt also treats malformed JSON and non-objects as an empty object

    val type = json.string("type")
    return when (type) {
        "TranscriptInterim",
        "TranscriptText" -> ClaudeVoiceEvent.Working(json.string("data").orEmpty())
        "TranscriptEndpoint" -> ClaudeVoiceEvent.Endpoint(json.string("data").orEmpty())
        "TranscriptError" -> ClaudeVoiceEvent.TranscriptError(json.redactedErrorSummary())
        else ->
            if (type == "error" || "error" in json) {
                ClaudeVoiceEvent.ServerError(json.redactedErrorSummary())
            } else {
                ClaudeVoiceEvent.Unknown
            }
    }
}

/**
 * Keeps only known descriptive fields, each cut to 240 chars. A field is read from the nested
 * `error` object when it holds a string there (even an empty one), otherwise from the top level.
 */
private fun JsonObject.redactedErrorSummary(): String {
    string("error")?.let {
        return it.take(MAX_ERROR_FIELD_CHARS)
    }
    val nested = this["error"] as? JsonObject
    return ERROR_SUMMARY_KEYS.mapNotNull { key ->
            val value = nested?.string(key) ?: string(key)
            if (value.isNullOrEmpty()) null else "$key=${value.take(MAX_ERROR_FIELD_CHARS)}"
        }
        .joinToString(" ")
}

/** The value at [key] if it is a JSON string; numbers, booleans and null don't count. */
private fun JsonObject.string(key: String): String? =
    (this[key] as? JsonPrimitive)?.takeIf { it.isString }?.content

private fun typedInterimsEnabled(env: (String) -> String?): Boolean =
    envFlag(env("CLAUDE_CODE_VOICE_FORWARD_INTERIMS_TYPED"), default = false) ||
        envFlag(env("SPEECHER_CLAUDE_FORWARD_INTERIMS_TYPED"), default = true)

private fun envFlag(value: String?, default: Boolean): Boolean =
    when (value?.trim(Char::isQtWhitespace)?.lowercase()) {
        "1",
        "true",
        "yes",
        "on" -> true
        "0",
        "false",
        "no",
        "off" -> false
        else -> default
    }

/** Trims, then replaces each internal whitespace run with one space. */
private fun String.simplified(): String = buildString {
    for (c in this@simplified.trim(Char::isQtWhitespace)) {
        when {
            !c.isQtWhitespace() -> append(c)
            last() != ' ' -> append(' ')
        }
    }
}

// QChar::isSpace(): unlike Char.isWhitespace(), it includes NEL (U+0085) and excludes
// U+001C..U+001F.
private fun Char.isQtWhitespace(): Boolean =
    this in '\t'..'\r' || this == '\u0085' || Character.isSpaceChar(this)

// Only ASCII letters fold, so "É" and "é" count as different terms.
private fun String.lowercaseAscii(): String = buildString {
    for (c in this@lowercaseAscii) append(if (c in 'A'..'Z') c.lowercaseChar() else c)
}
