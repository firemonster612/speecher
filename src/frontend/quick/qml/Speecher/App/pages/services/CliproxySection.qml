import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// CLI Proxy API's shared rows on Services, and under a sign-in card in a Fix
// sheet: the directory its account files are in, and the optional server
// cleanup goes through, with that server's key. ChatGPT and Claude share
// them; each card chooses only its own account.
Section {
    title: Schema.row("cliproxyOauthDir").section
    caption: Schema.row("cliproxyOauthDir").sectionHelp
    SchemaRow { rowId: "cliproxyOauthDir" }
    SchemaRow { rowId: "cliproxyBaseUrl" }
    SchemaRow { rowId: "cliproxyApiKey"; shown: !!Settings.values.cliproxyBaseUrl }
}
