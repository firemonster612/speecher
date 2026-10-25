pragma Singleton
import QtQuick

// The options the app was started with, as --name=value on the command line.
// The host passes the theme and motion settings this way; the preview tool
// also passes mock states and which form or tab a page opens with.
QtObject {
    function arg(name, fallback) {
        for (const a of Qt.application.arguments)
            if (a.startsWith("--" + name + "=")) return a.slice(name.length + 3)
        return fallback
    }
}
