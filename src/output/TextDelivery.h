#pragma once

#include "dictation/DictationPorts.h"
#include "output/ClipboardDelivery.h"

#include <functional>
#include <memory>
#include <QStringList>

namespace speecher {

class DeliveryBackend {
public:
    // htmlAvailable is a clipboard backend's alone.
    using Outcome = ClipboardCopied;
    virtual ~DeliveryBackend() = default;
    // A keyboard backend pastes what is already on the clipboard; a
    // clipboard backend copies content. done runs once with the outcome, and
    // may run before deliver() returns.
    // clearToInject is the final focus gate: a keyboard backend calls it after
    // all of its preparation (modifier release) and immediately before
    // sending the paste keystroke, aborting when it returns false.
    // Clipboard-only backends may ignore it; a null function means nothing
    // gates the injection.
    virtual void deliver(const DeliveryContent &content,
                         const std::function<bool()> &clearToInject,
                         std::function<void(const Outcome &)> done) = 0;
};

class TextDelivery : public TextDeliveryAdapter {
    Q_OBJECT

public:
    using BackendFactory = std::function<std::unique_ptr<DeliveryBackend>(
        const QString &method,
        const OutputSettings &settings,
        PasteMethod pasteMethod)>;

    explicit TextDelivery(QObject *parent = nullptr);
    explicit TextDelivery(TargetProvider *targetProvider, QObject *parent = nullptr);
    explicit TextDelivery(BackendFactory backendFactory, QObject *parent = nullptr);
    TextDelivery(BackendFactory backendFactory, TargetProvider *targetProvider, QObject *parent = nullptr);
    void deliver(const OutputSettings &settings,
                 const DeliveryContent &content,
                 const Target &target) override;
    // The steps deliver takes for this paste method and these settings, in
    // order: insert (straight into the app, through accessibility), press
    // (the paste keys) and copy (the words left on the clipboard).
    static QStringList pasteRoute(const OutputSettings &settings, PasteMethod pasteMethod);
    // What deliver needs of the computer to get words past the clipboard with
    // this paste method and these settings: nothing (it only copies, or
    // presses the paste keys itself), desktop accessibility (to insert, and
    // on macOS to press ⌘V), the virtual keyboard (to press the keys), or
    // either of them.
    enum class PasteNeed { Nothing, Accessibility, VirtualKeyboard, Either };
    static PasteNeed pasteNeed(const OutputSettings &settings, PasteMethod pasteMethod);
    // Whether words get no further than the clipboard: what need asks for is
    // missing. accessibility counts when it is on only for the session;
    // virtualKeyboard is the keyboard ready to type.
    static bool wordsStayCopied(PasteNeed need, bool accessibility, bool virtualKeyboard);
    static QStringList orderedMethods(const OutputSettings &settings);
    static QStringList orderedMethods(const OutputSettings &settings, PasteMethod pasteMethod);
    // How long a delivery may take before it is answered as unfinished and
    // its remaining steps dropped. Tests shorten it.
    void setAnswerDeadlineMs(int ms);

private:
    // The paste method deliver starts from, and whether it pastes blindly into
    // whatever has focus because the Target cannot be tracked.
    struct PasteChoice {
        PasteMethod method = PasteMethod::ClipboardOnly;
        bool currentFocusFallback = false;
    };
    class PreviousClipboard;
    // One delivery's steps, from the paste choice to the answer.
    class Run;

    void useDefaultBackendFactory();
    // The Paste Rule's method when the Target can be told apart from whatever
    // else is in front, or when a global rule pastes blindly into the current
    // focus; only the clipboard otherwise, and always for a secure field.
    static PasteChoice choosePasteMethod(const OutputSettings &settings, const Target &target);
    // Checks that the Target still has focus and readies it for the paste.
    // Returns why the paste keys must not go out, or nothing when they may.
    QString preparePasteInFocus(const Target &target);
    // Runs the first request, unless one already runs.
    void startNext();
    // Answers the running request, once, and moves on to the next.
    void answer(const DeliveryResult &result);

    struct Request {
        OutputSettings settings;
        DeliveryContent content;
        Target target;
    };
    // The delivery under way first, then those asked for meanwhile.
    QList<Request> m_requests;
    Run *m_run = nullptr;
    // Longer than every step's own time limit put together, so only a
    // delivery that is stuck runs into it.
    int m_answerDeadlineMs = 30000;
    ClipboardDelivery m_clipboardDelivery;
    BackendFactory m_backendFactory;
    TargetProvider *m_targetProvider = nullptr;
};

} // namespace speecher
