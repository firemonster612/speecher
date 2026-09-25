package app.speecher.protocol

import app.speecher.protocol.ClaudeVoiceEvent.Endpoint
import app.speecher.protocol.ClaudeVoiceEvent.ServerError
import app.speecher.protocol.ClaudeVoiceEvent.TranscriptError
import app.speecher.protocol.ClaudeVoiceEvent.Unknown
import app.speecher.protocol.ClaudeVoiceEvent.Working
import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Nested
import org.junit.jupiter.api.Test

class ClaudeVoiceProtocolTest {

    @Nested
    inner class StreamQuery {
        private val withTypedInterims =
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
        private val withoutTypedInterims = withTypedInterims - ("forward_interims" to "typed")

        private fun query(vararg env: Pair<String, String>) =
            claudeVoiceStreamQuery(env.toMap()::get)

        @Test
        fun `typed interims are on when no variable is set`() {
            assertEquals(withTypedInterims, query())
        }

        @Test
        fun `speecher variable turns typed interims off with any Qt-trimmed false spelling`() {
            for (disabled in listOf("0", "false", "NO", "\t Off \n", "\u0085off ")) {
                assertEquals(
                    withoutTypedInterims,
                    query("SPEECHER_CLAUDE_FORWARD_INTERIMS_TYPED" to disabled),
                )
            }
        }

        @Test
        fun `unrecognised or blank values keep the default`() {
            assertEquals(
                withTypedInterims,
                query("SPEECHER_CLAUDE_FORWARD_INTERIMS_TYPED" to "disabled"),
            )
            assertEquals(
                withTypedInterims,
                query("SPEECHER_CLAUDE_FORWARD_INTERIMS_TYPED" to "   "),
            )
        }

        @Test
        fun `claude code variable forces typed interims on with any Qt-trimmed true spelling`() {
            for (enabled in listOf("1", "true", "YES", "\u0085 On  ")) {
                assertEquals(
                    withTypedInterims,
                    query(
                        "SPEECHER_CLAUDE_FORWARD_INTERIMS_TYPED" to "0",
                        "CLAUDE_CODE_VOICE_FORWARD_INTERIMS_TYPED" to enabled,
                    ),
                )
            }
        }

        @Test
        fun `claude code variable cannot turn typed interims off`() {
            assertEquals(
                withTypedInterims,
                query("CLAUDE_CODE_VOICE_FORWARD_INTERIMS_TYPED" to "0"),
            )
        }
    }

    @Nested
    inner class KeytermsHeader {
        @Test
        fun `empty vocabulary gives an empty header`() {
            assertEquals("", claudeVoiceKeytermsHeader(emptyList()))
        }

        @Test
        fun `Qt whitespace is collapsed and blank terms are dropped`() {
            assertEquals(
                "Kotlin Coroutines,New York,a b,Gradle",
                claudeVoiceKeytermsHeader(
                    listOf("  Kotlin \t\n Coroutines\u0085", "", " ", " New York　", "a b", "Gradle")
                ),
            )
        }

        @Test
        fun `commas and non-whitespace controls stay raw`() {
            assertEquals(
                "a,b,\u001CX\u001F,a\u0000b",
                claudeVoiceKeytermsHeader(listOf("a,b", "\u001CX\u001F", "a\u0000b")),
            )
        }

        @Test
        fun `duplicates are case-insensitive and keep the first spelling`() {
            assertEquals(
                "Gradle,kotlin",
                claudeVoiceKeytermsHeader(listOf("Gradle", "kotlin", "GRADLE", " Kotlin ")),
            )
        }

        @Test
        fun `only ASCII letters are case-folded`() {
            assertEquals("Émile,émile", claudeVoiceKeytermsHeader(listOf("Émile", "émile")))
        }

        @Test
        fun `terms outside Latin-1 are dropped`() {
            assertEquals(
                "café,Straße,naïve",
                claudeVoiceKeytermsHeader(
                    listOf("café", "東京", "Straße", "€uro", "emoji 😀", "Ā", "naïve")
                ),
            )
        }

        @Test
        fun `a term exactly filling the limit is kept`() {
            val term = "a".repeat(1024)
            assertEquals(term, claudeVoiceKeytermsHeader(listOf(term, "b")))
        }

        @Test
        fun `a term longer than the limit is skipped`() {
            assertEquals("y", claudeVoiceKeytermsHeader(listOf("x".repeat(1025), "y")))
        }

        @Test
        fun `the separator counts towards the limit and later shorter terms still fit`() {
            val first = "é".repeat(1022)
            assertEquals("$first,c", claudeVoiceKeytermsHeader(listOf(first, "bb", "c", "d")))
        }
    }

    @Nested
    inner class EventParser {
        @Test
        fun `interim and text transcripts are working events with raw data`() {
            assertEquals(
                Working("hel"),
                parseClaudeVoiceEvent("""{"type":"TranscriptInterim","data":"hel"}"""),
            )
            assertEquals(
                Working(" hi  there "),
                parseClaudeVoiceEvent("""{"type":"TranscriptText","data":" hi  there "}"""),
            )
        }

        @Test
        fun `endpoint carries its data`() {
            assertEquals(
                Endpoint("done."),
                parseClaudeVoiceEvent("""{"type":"TranscriptEndpoint","data":"done."}"""),
            )
        }

        @Test
        fun `missing or non-string data becomes empty text`() {
            assertEquals(Endpoint(""), parseClaudeVoiceEvent("""{"type":"TranscriptEndpoint"}"""))
            for (data in listOf("null", "42", "true", "{}", "[]")) {
                assertEquals(
                    Working(""),
                    parseClaudeVoiceEvent("""{"type":"TranscriptText","data":$data}"""),
                )
            }
        }

        @Test
        fun `leading UTF-8 BOM is ignored`() {
            assertEquals(
                Working("BOM"),
                parseClaudeVoiceEvent("﻿{\"type\":\"TranscriptText\",\"data\":\"BOM\"}"),
            )
        }

        @Test
        fun `transcript type wins over an error field`() {
            assertEquals(
                Working("x"),
                parseClaudeVoiceEvent("""{"type":"TranscriptText","data":"x","error":"e"}"""),
            )
        }

        @Test
        fun `transcript error summary includes its own type`() {
            assertEquals(
                TranscriptError("type=TranscriptError code=E1 message=boom"),
                parseClaudeVoiceEvent(
                    """{"type":"TranscriptError","error":{"message":"boom","code":"E1"}}"""
                ),
            )
        }

        @Test
        fun `error summary keeps only known string fields, in fixed order`() {
            assertEquals(
                ServerError("type=error code=c error_code=ec message=m description=d"),
                parseClaudeVoiceEvent(
                    """{"description":"d","message":"m","error_code":"ec","code":"c","type":"error","status":401,"token":"secret"}"""
                ),
            )
        }

        @Test
        fun `a string error field is the whole summary`() {
            assertEquals(
                ServerError("rate limited"),
                parseClaudeVoiceEvent("""{"error":"rate limited","message":"ignored"}"""),
            )
            assertEquals(
                ServerError(""),
                parseClaudeVoiceEvent("""{"error":"","message":"ignored"}"""),
            )
        }

        @Test
        fun `a string error field and each summary field are cut to 240 UTF-16 units`() {
            assertEquals(
                ServerError("e".repeat(240)),
                parseClaudeVoiceEvent("""{"error":"${"e".repeat(300)}"}"""),
            )
            assertEquals(
                ServerError("type=error message=${"m".repeat(240)}"),
                parseClaudeVoiceEvent("""{"type":"error","message":"${"m".repeat(241)}"}"""),
            )
            assertEquals(
                ServerError("a".repeat(239) + '\uD83D'),
                parseClaudeVoiceEvent("""{"error":"${"a".repeat(239)}😀"}"""),
            )
        }

        @Test
        fun `nested string fields win, even when empty`() {
            assertEquals(
                ServerError("type=invalid_request"),
                parseClaudeVoiceEvent(
                    """{"type":"error","error":{"type":"invalid_request","message":""},"message":"top"}"""
                ),
            )
        }

        @Test
        fun `non-string nested fields fall back to the top level`() {
            assertEquals(
                ServerError("code=top error_code=quota"),
                parseClaudeVoiceEvent(
                    """{"error":{"code":500,"error_code":"quota"},"code":"top"}"""
                ),
            )
        }

        @Test
        fun `an error field of any non-string kind marks an error summarised from the top level`() {
            for (error in listOf("null", "false", "17", "[]", "{}")) {
                assertEquals(
                    ServerError("message=m"),
                    parseClaudeVoiceEvent("""{"error":$error,"message":"m"}"""),
                )
            }
        }

        @Test
        fun `anything else is unknown`() {
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
                .forEach { assertEquals(Unknown, parseClaudeVoiceEvent(it), it) }
        }
    }
}
