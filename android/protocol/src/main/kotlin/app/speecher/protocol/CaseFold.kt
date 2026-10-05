package app.speecher.protocol

/**
 * [text] with each code point replaced by its Unicode simple case folding, as Qt's
 * QChar::toCaseFolded does and so as QString compares ignoring case. Unlike Kotlin's ignoreCase,
 * the Turkish dotted İ and dotless ı fold to themselves, so neither equals i or I.
 */
fun foldCase(text: String): String = buildString {
    text.codePoints().forEach { c ->
        appendCodePoint(
            when (c) {
                DOTTED_CAPITAL_I,
                DOTLESS_SMALL_I -> c
                // Cherokee folds to its capital letters, the ones encoded first.
                in CHEROKEE,
                in CHEROKEE_SMALL_LETTERS -> Character.toUpperCase(c)
                else -> Character.toLowerCase(Character.toUpperCase(c))
            }
        )
    }
}

private const val DOTTED_CAPITAL_I = 0x130
private const val DOTLESS_SMALL_I = 0x131
private val CHEROKEE = 0x13A0..0x13FD
private val CHEROKEE_SMALL_LETTERS = 0xAB70..0xABBF
