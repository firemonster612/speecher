package app.speecher.protocol

import app.speecher.protocol.ClaudeVoiceEvent.Endpoint
import app.speecher.protocol.ClaudeVoiceEvent.ServerError
import app.speecher.protocol.ClaudeVoiceEvent.TranscriptError
import app.speecher.protocol.ClaudeVoiceEvent.Unknown
import app.speecher.protocol.ClaudeVoiceEvent.Working
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

class ClaudeVoiceProtocolTest {
    private val english =
        listOf(
            "encoding" to "linear16",
            "sample_rate" to "16000",
            "channels" to "1",
            "endpointing_ms" to "300",
            "utterance_end_ms" to "1000",
            "language" to "en",
            "use_conversation_engine" to "true",
            "forward_interims" to "typed",
            "stt_provider" to "deepgram-nova3",
        )

    @Test
    fun `the stream query names the spoken language, and none for Automatic`() {
        val german = english.map { if (it.first == "language") "language" to "de" else it }
        assertEquals(german, claudeVoiceStreamQuery("de") { null })
        assertEquals(english - ("language" to "en"), claudeVoiceStreamQuery("auto") { null })
    }

    @Test
    fun `typed interims stay on unless Speecher's variable says false and Claude Code's not true`() {
        val speecher = "SPEECHER_CLAUDE_FORWARD_INTERIMS_TYPED"
        val claudeCode = "CLAUDE_CODE_VOICE_FORWARD_INTERIMS_TYPED"
        // Spellings as Qt reads them: any case, trimmed of Unicode whitespace.
        val off =
            listOf("0", "false", "NO", "\t Off \n", "\u0085off\u00A0").map { mapOf(speecher to it) }
        val on =
            listOf(emptyMap(), mapOf(speecher to "disabled"), mapOf(speecher to "   ")) +
                mapOf(claudeCode to "0") +
                listOf("1", "true", "YES", "\u0085 On \u00A0").map {
                    mapOf(speecher to "0", claudeCode to it)
                }
        val untyped = english - ("forward_interims" to "typed")
        assertEquals(
            off.map { untyped } + on.map { english },
            (off + on).map { env -> claudeVoiceStreamQuery("en", env::get) },
        )
    }

    @Test
    fun `keyterms join trimmed, collapsed, first spellings of distinct terms, raw otherwise`() {
        val cases =
            listOf(
                emptyList<String>() to "",
                listOf(
                    "  Kotlin \t\n Coroutines\u0085",
                    "",
                    "\u00A0",
                    "\u2003New York\u3000",
                    "a b",
                    "Gradle",
                ) to "Kotlin Coroutines,New York,a b,Gradle",
                listOf("a,b", "\u001CX\u001F", "a\u0000b") to "a,b,\u001CX\u001F,a\u0000b",
                // Duplicates differ only in ASCII case.
                listOf("Gradle", "kotlin", "GRADLE", " Kotlin ", "Émile", "émile") to
                    "Gradle,kotlin,Émile,émile",
                listOf("café", "東京", "Straße", "€uro", "emoji 😀") to
                    "café,東京,Straße,€uro,emoji 😀",
            )
        assertEquals(cases.map { it.second }, cases.map { claudeVoiceKeytermsHeader(it.first) })
        assertEquals(
            listOf("Gradle", "Kotlin Coroutines", "東京"),
            claudeVoiceKeyterms(listOf("Gradle", "gradle", " Kotlin  Coroutines ", "東京")),
        )
    }

    @Test
    fun `keyterms fill 1024 UTF-8 bytes, separators included, skipping terms that don't fit`() {
        val full = "a".repeat(1024)
        val nearlyFull = "é".repeat(511) // 1022 bytes
        val cases =
            listOf(
                listOf(full, "b") to full,
                listOf("x".repeat(1025), "y") to "y",
                // 342 characters, but 1026 bytes.
                listOf("東".repeat(342), "y") to "y",
                listOf(nearlyFull, "bb", "c", "d") to "$nearlyFull,c",
            )
        assertEquals(cases.map { it.second }, cases.map { claudeVoiceKeytermsHeader(it.first) })
    }

    @Test
    fun `transcripts carry their raw data, empty when missing or not a string`() {
        val cases =
            listOf(
                """{"type":"TranscriptInterim","data":"hel"}""" to Working("hel"),
                """{"type":"TranscriptText","data":" hi  there "}""" to Working(" hi  there "),
                """{"type":"TranscriptEndpoint","data":"done."}""" to Endpoint("done."),
                """{"type":"TranscriptEndpoint"}""" to Endpoint(""),
                "﻿{\"type\":\"TranscriptText\",\"data\":\"BOM\"}" to Working("BOM"),
                // The transcript type wins over an error field.
                """{"type":"TranscriptText","data":"x","error":"e"}""" to Working("x"),
            ) +
                listOf("null", "42", "true", "{}", "[]").map {
                    """{"type":"TranscriptText","data":$it}""" to Working("")
                }
        assertEquals(cases.map { it.second }, cases.map { parseClaudeVoiceEvent(it.first) })
    }

    @Test
    fun `errors are summarised from known string fields, nested first, each cut to 240 units`() {
        val cases =
            listOf(
                """{"type":"TranscriptError","error":{"message":"boom","code":"E1"}}""" to
                    TranscriptError("type=TranscriptError code=E1 message=boom"),
                """{"description":"d","message":"m","error_code":"ec","code":"c","type":"error","status":401,"token":"secret"}""" to
                    ServerError("type=error code=c error_code=ec message=m description=d"),
                """{"error":"rate limited","message":"ignored"}""" to ServerError("rate limited"),
                """{"error":"","message":"ignored"}""" to ServerError(""),
                """{"error":"${"e".repeat(300)}"}""" to ServerError("e".repeat(240)),
                """{"type":"error","message":"${"m".repeat(241)}"}""" to
                    ServerError("type=error message=${"m".repeat(240)}"),
                """{"error":"${"a".repeat(239)}😀"}""" to ServerError("a".repeat(239) + '\uD83D'),
                """{"type":"error","error":{"type":"invalid_request","message":""},"message":"top"}""" to
                    ServerError("type=invalid_request"),
                """{"error":{"code":500,"error_code":"quota"},"code":"top"}""" to
                    ServerError("code=top error_code=quota"),
            ) +
                listOf("null", "false", "17", "[]", "{}").map {
                    """{"error":$it,"message":"m"}""" to ServerError("message=m")
                }
        assertEquals(cases.map { it.second }, cases.map { parseClaudeVoiceEvent(it.first) })
    }

    @Test
    fun `anything else is unknown`() {
        val others =
            listOf(
                """{"type":"KeepAlive"}""",
                """{"type":"transcripttext","data":"ignored"}""",
                """{"type":5,"data":"x"}""",
                """{}""",
                """[{"error":"x"}]""",
                "\"error\"",
                "not json",
                "{'type':'error'}",
                "",
            )
        assertEquals(others.map { Unknown }, others.map(::parseClaudeVoiceEvent))
    }
}
