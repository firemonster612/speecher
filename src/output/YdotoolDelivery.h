#pragma once

#include "core/PasteRules.h"

#include <QObject>
#include <QString>

#include <functional>

namespace speecher {

class YdotoolDelivery : public QObject {
    Q_OBJECT

public:
    explicit YdotoolDelivery(QObject *parent = nullptr);
    // Waits for ydotool, for the setup's test typing; delivery pastes instead.
    bool type(const QString &text, QString *error = nullptr);
    // Presses the paste keys over what is on the clipboard, and returns at
    // once: ydotool releases the modifiers first and after, and done runs
    // once at the end, unless this is gone by then. clearToInject runs after
    // the first release, immediately before the paste keystroke; false aborts
    // without sending it.
    void pasteFromClipboard(const QString &text,
                            PasteMethod method,
                            const std::function<bool()> &clearToInject,
                            std::function<void(bool pasted, const QString &error)> done);
    static bool isAvailable();
    static QString socketPath();
    static QStringList commandArguments(const QString &text);
    static QStringList pasteShortcutArguments(PasteMethod method);
    static QString withoutTrailingWhitespace(const QString &text);
};

} // namespace speecher
