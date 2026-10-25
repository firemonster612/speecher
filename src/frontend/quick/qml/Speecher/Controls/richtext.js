// Helpers for text shown as rich text (Text.StyledText or RichText).
.pragma library

// s with the characters rich text treats as markup escaped, so it shows as
// written. Callers add their own markup around the result.
function escape(s) {
    return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
}

// One line of Markdown as RichText: `code` in monoFamily, [text](url) links
// in linkColor and **bold**; anything else shows as written. Text.MarkdownText
// would do more, but it colours links and sizes code its own way, and
// StyledText cannot change the font family.
function inlineMarkdown(s, monoFamily, linkColor) {
    // Odd parts sit between backticks; an unpaired last backtick stays text.
    const parts = escape(s).split("`")
    if (parts.length % 2 === 0) {
        const tail = parts.pop()
        parts.push(parts.pop() + "`" + tail)
    }
    return parts.map((part, i) => i % 2 ? "<span style=\"font-family: '" + monoFamily + "'\">" + part + "</span>"
        : part.replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, "<a href=\"$2\" style=\"color: " + linkColor + "\">$1</a>")
              .replace(/\*\*(.+?)\*\*/g, "<b>$1</b>")).join("")
}
