package app.speecher.protocol

import org.junit.jupiter.api.Assertions.assertEquals
import org.junit.jupiter.api.Test

/** Expected values are Unicode CaseFolding.txt's simple (C and S) mappings, which Qt uses. */
class CaseFoldTest {
    @Test
    fun `I folds to i, but the Turkish dotted and dotless I stay themselves`() {
        assertEquals("iiİı", foldCase("Iiİı"))
    }

    @Test
    fun `letters fold to one code point each, including those without a plain lowercase`() {
        // ẞ ſ µ ς ϐ Ω-sign K-sign, then Cherokee small and capital, then Deseret capital.
        assertEquals(
            "ßsμσβωkᎠᎠᏰ𐐨",
            foldCase("ẞſµςϐΩKꭰᎠᏸ𐐀"),
        )
    }

    @Test
    fun `words fold whole, with final sigma and the Cherokee small letters going up`() {
        assertEquals("i ı İ i", foldCase("I ı İ i"))
        assertEquals("οδοσ σ", foldCase("ΟΔΟΣ ς"))
        assertEquals("straße ß s μ", foldCase("Straße ẞ ſ µ"))
        assertEquals("\u13A0\u13F0 \u13A0", foldCase("\uAB70\u13F8 \u13A0"))
        assertEquals("\uD801\uDC28", foldCase("\uD801\uDC00"))
    }
}
