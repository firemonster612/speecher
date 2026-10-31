#pragma once

#include "core/PasteRules.h"
#include "output/ProcessRunner.h"

#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace speecher {

class YdotoolDelivery : public QObject {
    Q_OBJECT

public:
    explicit YdotoolDelivery(QObject *parent = nullptr);
    // Gone mid-paste: the paste keys stop, and the modifiers are let go all
    // the same.
    ~YdotoolDelivery() override;
    // Waits for ydotool, for the setup's test typing; delivery pastes instead.
    bool type(const QString &text, QString *error = nullptr);
    // Presses the paste keys over what is on the clipboard, and returns at
    // once: ydotool releases the modifiers first and after, and done runs
    // once at the end, unless this is gone by then. clearToInject is asked
    // after the first release, and the paste keystroke goes out from its
    // reply; a refusal aborts without sending it.
    void pasteFromClipboard(const QString &text,
                            PasteMethod method,
                            const std::function<void(std::function<void(bool clear)>)> &clearToInject,
                            std::function<void(bool pasted, const QString &error)> done);
    static bool isAvailable();
    static QString socketPath();
    static QStringList commandArguments(const QString &text);
    static QStringList pasteShortcutArguments(PasteMethod method);
    static QString withoutTrailingWhitespace(const QString &text);

private:
    // The paste keystroke, then the release after it.
    void pressPasteKeys(const ProcessCommand &paste,
                        const ProcessCommand &release,
                        std::function<void(bool, const QString &)> done);
    // The paste keystroke under way, and the release that must follow it.
    struct PasteUnderWay {
        std::function<void()> stop;
        ProcessCommand release;
    };
    std::optional<PasteUnderWay> m_pasteUnderWay;
};

} // namespace speecher
