import QtQuick

// The Select for a value a narrower place (an App, an App Type, a Writing
// Profile, one run) may change: its first option, id "", is the wider
// place's value, "Same as <place> (<value>)" as Words.sameAs words it,
// marked with a link, and choosing it hands the value back to the wider
// place. choices are the other options. Without inheritedText it is a plain
// Select, for the widest place.
Select {
    property string inheritedText
    property var choices: []

    options: (inheritedText ? [{ id: "", label: inheritedText, icon: "link" }] : []).concat(choices)
}
