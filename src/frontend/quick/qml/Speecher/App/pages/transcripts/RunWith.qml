pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "RunWords.js" as T

// What Transcribe files and Recordings make a transcript with, in one shape:
// the Writing Profile, "Same as your settings (…)" until one is chosen, then
// what the run changes of it, folded under a row that says how much, then
// each service's status with its Fix while it can't run. The page's own
// rows go in the fold.
Section {
    id: root

    // The profile chosen, "" for the settings' own, and the settings' own.
    property string profileId
    property string inheritedProfile
    property string profileHelp
    property string changesTitle
    property string changesHelp
    // How many of the profile's settings the run changes.
    property int changes: 0
    // What the fold says while the run changes nothing: what its rows
    // follow, the profile unless the page says otherwise.
    property string sameText: T.changesSummary(0, Library.profileName(profileId || inheritedProfile))
    // The services the run hears and refines with, "" (or "none") for none,
    // whose status shows under the changes.
    property string speechService
    property string refinementService
    default property alias changesContent: fold.content
    signal profileChosen(string id)

    function openChanges() { fold.open = true }

    title: T.text.runWith

    SettingRow {
        title: T.text.profile
        help: root.profileHelp
        InheritSelect {
            objectName: "runProfile"
            inheritedText: Words.sameAs(T.text.yourSettings, Library.profileName(root.inheritedProfile))
            choices: Library.profiles.map(p => ({ id: p.id, label: p.name, icon: Library.profileIcon(p.id) }))
            currentId: root.profileId
            onChosen: id => root.profileChosen(id)
        }
    }
    Disclosure {
        id: fold
        title: root.changesTitle
        help: root.changesHelp
        trailingText: root.changes === 0 ? root.sameText : T.changesSummary(root.changes, "")
        open: root.changes > 0
    }
    ServiceStatus { role: "speech"; serviceId: root.speechService }
    ServiceStatus { role: "refinement"; serviceId: root.refinementService }
}
