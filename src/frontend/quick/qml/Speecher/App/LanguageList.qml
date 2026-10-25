import QtQuick
import Speecher.Controls
import Speecher.Wording

// The languages a speech service or Local Model listens for, named as the
// Spoken language row names them, in a LanguagePopover that only shows
// them. show(at, codes) opens it from the count that was clicked.
LanguagePopover {
    id: list

    property var codes: []

    function show(at, languageCodes) {
        codes = languageCodes
        openFrom(at)
    }

    align: "left"
    languages: ModelText.languageOptions(codes)
    searchText: Words.searchLanguages
    clearText: Words.clear
    emptyText: Words.noLanguage
}
