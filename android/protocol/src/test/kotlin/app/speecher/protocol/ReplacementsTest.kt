package app.speecher.protocol

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Assertions.assertFalse
import org.junit.jupiter.api.Assertions.assertNull
import org.junit.jupiter.api.Assertions.assertThrows
import org.junit.jupiter.api.Assertions.assertTrue
import org.junit.jupiter.api.Test

// Cases and expected values from the desktop's tests/test_bindings.cpp.
class ReplacementsTest {
    private val email = Replacement("my email", "efox@example.com")
    private val phone = Replacement("my phone", "+1 555 0100")

    @Test
    fun `phrases match ignoring case and punctuation, and covering every word skips refinement`() {
        assertEquals("my email c repo path", normalizedPhrase(" My, EMAIL! C++ repo_path "))
        val replaced = replaceSpoken("My, email! my phone", listOf(email, phone))
        assertEquals("efox@example.com! +1 555 0100", replaced.text)
        assertEquals("SPEECHER_BINDING_0! SPEECHER_BINDING_1", replaced.refinementInput)
        assertTrue(replaced.skipsRefinement)
    }

    @Test
    fun `the longest phrase wins and glued words do not match`() {
        val replaced =
            replaceSpoken(
                "open main repo and repo mainrepo",
                listOf(Replacement("repo", "R"), Replacement("main repo", "M")),
            )
        assertEquals("open M and R mainrepo", replaced.text)
        assertEquals(
            "open SPEECHER_BINDING_0 and SPEECHER_BINDING_1 mainrepo",
            replaced.refinementInput,
        )
        assertFalse(replaced.skipsRefinement)
    }

    @Test
    fun `restoring refined text keeps placeholders exact and replaces phrases the model repaired`() {
        val replaced = replaceSpoken("my email and my email", listOf(email, phone))
        assertEquals(
            "Please send efox@example.com.",
            replaced.restore("Please send SPEECHER_BINDING_0."),
        )
        assertEquals("Please send Alex.", replaced.restore("Please send Alex."))
        assertEquals(
            "efox@example.com, then +1 555 0100",
            replaced.restore(" SPEECHER_BINDING_1, then my phone "),
        )
        assertNull(replaced.restore("Please send SPEECHER_BINDING_99."))
        assertNull(replaced.restore("Please send Speecher binding 0."))
        assertNull(replaced.restore("Please send Speecher binding zero."))
        assertNull(replaced.restore("  "))
    }

    @Test
    fun `the preview shows each placeholder's text and hides one still streaming in`() {
        val replaced = replaceSpoken("send my email", listOf(email))
        assertEquals("Send efox@example.com now", replaced.preview("Send SPEECHER_BINDING_0 now"))
        assertEquals("Send ", replaced.preview("Send SPEECHER_BIN"))
        assertEquals("Send ", replaced.preview("Send SPEECHER_BINDING_0"))
    }

    @Test
    fun `a spoken no-bind directive keeps the phrase it points at literal`() {
        val rules = listOf(email, phone)
        // "that" points back to the nearest phrase before the directive.
        assertEquals(
            "please write efox@example.com and my phone but don't turn that into a binding",
            replaceSpoken(
                    "please write my email and my phone but don't turn that into a binding",
                    rules,
                )
                .text,
        )
        assertEquals(
            "write my email, don't bind that, then write +1 555 0100",
            replaceSpoken("write my email, don't bind that, then write my phone", rules).text,
        )
        // Without "that", the phrase after the action is the target.
        val named = replaceSpoken("don't replace my email", rules)
        assertEquals("don't replace my email", named.text)
        assertEquals("Don't replace my email.", named.restore("Don't replace my email."))
    }

    @Test
    fun `a directive that names no phrase replaces nothing the model writes`() {
        val replaced = replaceSpoken("write my phone, but don't bind my evil", listOf(email, phone))
        assertEquals("write +1 555 0100, but don't bind my evil", replaced.text)
        assertEquals(
            "Write +1 555 0100 and my email.",
            replaced.restore("Write SPEECHER_BINDING_0 and my email."),
        )
    }

    @Test
    fun `problems name each rule that cannot be saved`() {
        assertEquals(
            listOf(
                "Row 2 duplicates the normalized spoken phrase from row 1.",
                "Row 3 needs a spoken phrase containing at least one letter or number.",
                "Row 4 needs replacement text.",
            ),
            replacementProblems(
                listOf(
                    Replacement("my,email", "one"),
                    Replacement("MY email", "two"),
                    Replacement("++", "symbols only"),
                    Replacement("empty replacement", "   "),
                )
            ),
        )
    }

    @Test
    fun `aliases are each phrase as written and normalized, once each`() {
        assertEquals(
            listOf("my,email", "my email", "speecher repo"),
            replacementAliases(
                listOf(
                    Replacement("my,email", "efox@example.com"),
                    Replacement("speecher repo", "/home/efox/projects/speecher3"),
                )
            ),
        )
    }

    @Test
    fun `date and time fill in and other braces stay`() {
        assertEquals(
            listOf(Replacement("sent", "Sent on 05/10/2026 at 09:41 {name}")),
            withVariablesFilled(
                listOf(Replacement("sent", "Sent on {date} at {time} {name}")),
                "05/10/2026",
                "09:41",
            ),
        )
    }

    @Test
    fun `JSON import adds snippet arrays and phrase maps after the rules there, or says why not`() {
        assertEquals(
            listOf(email, Replacement("sign off", "Regards,\nEfox")),
            importReplacements(
                listOf(email),
                "\uFEFF" + """{"snippets":[{"trigger":"sign off","expansion":"Regards,\nEfox"}]}""",
            ),
        )
        assertEquals(
            listOf(
                Replacement("email me", "efox@example.com"),
                Replacement("home address", "123 Main Street"),
            ),
            importReplacements(
                emptyList(),
                """{"home address":"123 Main Street","email me":"efox@example.com"}""",
            ),
        )
        assertEquals(
            "Row 3 duplicates the normalized spoken phrase from row 1.",
            assertThrows(IllegalArgumentException::class.java) {
                    importReplacements(
                        listOf(email),
                        """[{"phrase":"sign off","text":"1"},{"phrase":"My email!","text":"2"}]""",
                    )
                }
                .message,
        )
        assertEquals(
            "Invalid JSON.",
            assertThrows(IllegalArgumentException::class.java) {
                    importReplacements(emptyList(), "{")
                }
                .message,
        )
    }
}
