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
}
