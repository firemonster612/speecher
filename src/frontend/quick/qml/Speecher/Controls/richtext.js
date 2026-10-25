// Helpers for text shown as rich text (Text.StyledText or RichText).
.pragma library

// s with the characters rich text treats as markup escaped, so it shows as
// written. Callers add their own markup around the result.
function escape(s) {
    return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;")
}
