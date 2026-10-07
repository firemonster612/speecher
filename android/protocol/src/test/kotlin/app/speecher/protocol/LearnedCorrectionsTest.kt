package app.speecher.protocol

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

// Cases from the desktop's tests/test_settings.cpp, test_platform_composition.cpp,
// test_delivery.cpp and test_vocabulary.cpp.
class LearnedCorrectionsTest {
    @Test
    fun `analysis narrows an edit to the changed words`() {
        assertEquals(
            CorrectionEvidence("cute", "Qt", 0.75),
            analyzeCorrection("I use cute every day", "I use Qt every day"),
        )
        assertEquals(CorrectionEvidence("qt", "Qt", 0.98), analyzeCorrection("qt", "Qt"))
        assertEquals(
            CorrectionEvidence("open ai", "OpenAI", 0.98),
            analyzeCorrection("Use open ai here", "Use OpenAI here"),
        )
    }

    @Test
    fun `analysis rejects rewrites, grammar, punctuation and secrets`() {
        listOf(
                "unchanged" to "unchanged",
                "hello" to "hello there",
                "hello there" to "hello",
                "file" to "files",
                "files" to "file",
                "hello" to "hello!",
                "hello!" to "hello",
                "cute and plasma" to "Qt and Plasma",
                "cat dog" to "bat fog",
                "token=old-value" to "token=abcdefghijklmnopqrstuv",
                "email a@example.test" to "email b@example.test",
                "card 4111 1111 1111 1111" to "card 4111 1111 1111 1112",
                "card 4111-1111-1111-1111" to "card 4111-1111-1111-1112",
                "this entire sentence is being rewritten into something else" to
                    "a completely unrelated and substantially different paragraph",
            )
            .forEach { (inserted, edited) ->
                assertNull(analyzeCorrection(inserted, edited), "$inserted -> $edited")
            }
    }

    private val window =
        CorrectionWindow(
            "I use cute every day",
            "before text, long enough ",
            " after text, long enough.",
        )

    @Test
    fun `a correction counts once two readings agree`() {
        val tracker = CorrectionTracker(window)
        assertNull(
            tracker.sample("before text, long enough I use cute every day after text, long enough.")
        )
        assertNull(
            tracker.sample("before text, long enough I use Qt every day after text, long enough.")
        )
        assertEquals(
            CorrectionEvidence("cute", "Qt", 0.75),
            tracker.sample("before text, long enough I use Qt every day after text, long enough."),
        )
        assertFalse(tracker.active)
    }

    @Test
    fun `a reading that is not a correction starts the count again`() {
        val tracker = CorrectionTracker(window)
        val pinned = { span: String -> "before text, long enough $span after text, long enough." }
        assertNull(tracker.sample(pinned("I use Q every day")))
        assertNull(tracker.sample(pinned("I use an entirely different phrase today")))
        assertNull(tracker.sample(pinned("I use Qt every day")))
        assertEquals(
            CorrectionEvidence("cute", "Qt", 0.75),
            tracker.sample(pinned("I use Qt every day")),
        )
    }

    @Test
    fun `losing or repeating a pin ends the observation`() {
        val lost = CorrectionTracker(window)
        lost.sample("before text, long enough I use Qt every day")
        assertFalse(lost.active)

        val repeated = CorrectionTracker(window)
        repeated.sample(
            "before text, long enough I use Qt every day after text, long enough. " +
                "before text, long enough again"
        )
        assertFalse(repeated.active)
    }

    @Test
    fun `a short pin holds the span to the field's edge`() {
        // The usual phone case: a long message, the dictation at its end.
        val message = "Thanks for the update. Can you "
        val ending = CorrectionWindow("ping the team on speaker", message, "")
        assertTrue(insertionLanded("${message}ping the team on speaker", ending))
        assertFalse(insertionLanded("Hey, ping the team on speaker", ending))
        val tracker = CorrectionTracker(ending)
        assertNull(tracker.sample("${message}ping the team on Speecher"))
        assertEquals(
            CorrectionEvidence("speaker", "Speecher", 0.75),
            tracker.sample("${message}ping the team on Speecher"),
        )

        // Typing on after the dictation widens the span past a correction.
        val typedOn = CorrectionTracker(CorrectionWindow("ping the team on speaker", "Hey, ", ""))
        typedOn.sample("Hey, ping the team on Speecher today please")
        assertNull(typedOn.sample("Hey, ping the team on Speecher today please"))

        val cleared = CorrectionTracker(CorrectionWindow("ping the team", "", ""))
        cleared.sample("")
        assertFalse(cleared.active)
    }

    private fun record(
        corrections: Corrections,
        evidence: CorrectionEvidence,
        app: String,
        nowMs: Long = 100,
    ) = withEvidence(corrections, evidence, app, nowMs) { "id${corrections.learned.size}" }

    @Test
    fun `an edit is learned the second time, or at once when only its spelling changed`() {
        val medium = CorrectionEvidence("cute", "Qt", 0.75)
        val once = record(Corrections(), medium, "org.kde.kate", 100)
        assertEquals(
            Corrections(
                pending = listOf(PendingCorrection("cute", "Qt", "org.kde.kate", 1, 100, 100, 0.75))
            ),
            once,
        )
        val twice = record(once, medium, "org.kde.kate", 200)
        assertEquals(
            Corrections(
                listOf(
                    LearnedCorrection("id0", "cute", "Qt", "org.kde.kate", 100, 0.75, true, 2, 200)
                )
            ),
            twice,
        )
        assertEquals(3, record(twice, medium, "org.kde.kate").learned.single().evidenceCount)

        val spelling =
            record(Corrections(), CorrectionEvidence("open ai", "OpenAI", 0.98), "org.kde.kate")
        assertEquals(
            listOf(LearnedCorrection("id0", "open ai", "OpenAI", "org.kde.kate", 100, 0.98)),
            spelling.learned,
        )
        // A contradiction does not overwrite what was learned, and too unsure evidence is dropped.
        assertEquals(
            spelling,
            record(spelling, CorrectionEvidence("open ai", "Open API", 0.98), "org.kde.kate"),
        )
        assertEquals(
            Corrections(),
            record(Corrections(), CorrectionEvidence("cute", "Qt", 0.4), "org.kde.kate"),
        )
    }

    @Test
    fun `the same edit in a second app applies in every app`() {
        val spelling = CorrectionEvidence("open ai", "OpenAI", 0.98)
        val learned =
            record(
                record(Corrections(), spelling, "org.kde.kate"),
                spelling,
                "org.mozilla.firefox",
                200,
            )
        assertEquals(
            listOf(LearnedCorrection("id0", "open ai", "OpenAI", "", 100, 0.98, true, 2, 200)),
            learned.learned,
        )

        val medium = CorrectionEvidence("cute", "Qt", 0.75)
        val pending =
            record(record(Corrections(), medium, "org.kde.kate"), medium, "org.mozilla.firefox")
        assertEquals("", pending.learned.single().applicationId)
    }

    private val learned =
        listOf(
            LearnedCorrection("0", "post grass", "global choice", "", 1, 0.98),
            LearnedCorrection("1", "cute", "Qt", "org.kde.kate", 1, 0.98),
            LearnedCorrection("2", "post grass", "Postgres", "org.kde.kate", 1, 0.75, true, 2, 2),
            LearnedCorrection("3", "open ai", "OpenAI", "", 1, 0.98, true, 2, 2),
            LearnedCorrection("4", "get hub", "GitHub", "", 1, 0.98, enabled = false),
        )

    @Test
    fun `corrections replace after the person's own rules, this app's first`() {
        val rules = listOf(Replacement("cute", "user choice"))
        val spoken = "cute uses post grass and open ai on get hub"
        assertEquals(
            "user choice uses Postgres and OpenAI on get hub",
            replaceSpoken(spoken, withLearnedCorrections(rules, learned, "ORG.KDE.KATE")).text,
        )
        assertEquals(
            "user choice uses global choice and OpenAI on get hub",
            replaceSpoken(spoken, withLearnedCorrections(rules, learned, "org.mozilla.firefox"))
                .text,
        )
    }

    @Test
    fun `corrections follow the vocabulary in speech terms and lead it in refinement`() {
        val vocabulary =
            listOf(
                VocabularyWord("Speecher"),
                VocabularyWord("Postgres", "The database.", setOf(WritingProfile.Work)),
            )
        assertEquals(
            listOf("Speecher", "global choice", "Qt", "OpenAI"),
            speechTerms(vocabulary, WritingProfile.Personal, learned),
        )
        assertEquals(
            listOf(
                VocabularyWord("global choice", source = "learned"),
                VocabularyWord("Qt", source = "learned"),
                VocabularyWord("Postgres", "The database.", source = "learned"),
                VocabularyWord("OpenAI", source = "learned"),
                VocabularyWord("Speecher"),
            ),
            refinementVocabulary(vocabulary, WritingProfile.Work, learned),
        )
        assertEquals(
            listOf("global choice", "Qt", "OpenAI", "Speecher"),
            refinementVocabulary(vocabulary, WritingProfile.Personal, learned).map { it.term },
        )
    }

    @Test
    fun `a full speech request leaves the corrections out`() {
        val full = List(MAX_SPEECH_TERMS) { VocabularyWord("term$it") }
        assertFalse("Qt" in speechTerms(full, WritingProfile.Other, learned))
    }
}
