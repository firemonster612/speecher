pragma Singleton
import QtQuick

// Mock of core's shared wording: the words many places use, such as the
// labels and tooltips pages hand to shared controls, which have none of
// their own, and the helpers that word numbers and lists.
QtObject {
    // "1,284"
    function num(n, decimals) { return Number(n).toLocaleString(Qt.locale(), "f", decimals || 0) }
    // "1 term", "128 terms"
    function count(n, one, many) { return num(n) + " " + (n === 1 ? one : many) }
    // "42%", from 0.42
    function percent(fraction) { return Math.round(fraction * 100) + "%" }
    // "Slack, Linear and Notion"
    function list(names) { return names.length < 2 ? names.join("") : names.slice(0, -1).join(", ") + " and " + names[names.length - 1] }

    readonly property string cancel: "Cancel"
    readonly property string save: "Save"
    readonly property string edit: "Edit"
    readonly property string delete_: "Delete"
    readonly property string remove: "Remove"
    readonly property string moveUp: "Move up"
    readonly property string moveDown: "Move down"
    readonly property string copy: "Copy"
    readonly property string copied: "Copied"
    readonly property string show: "Show"
    readonly property string hide: "Hide"
    readonly property string clear: "Clear"
    readonly property string choose: "Choose…"
    readonly property string suggestions: "Suggestions"
    readonly property string recommended: "Recommended"
    readonly property string turnOn: "Turn on"
    readonly property string checkAgain: "Check again"
    readonly property string more: "More"
    readonly property string pause: "Pause"
    readonly property string resume: "Resume"
    // A shortcut field: unset, listening for keys, and its clear button.
    readonly property string notSet: "Not set"
    readonly property string pressShortcut: "Press a shortcut…"
    readonly property string clearShortcut: "Clear shortcut"
}
