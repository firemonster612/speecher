#pragma once

#include "dictation/DictationPorts.h"
#include "output/ClipboardDelivery.h"

#include <functional>
#include <memory>
#include <optional>
#include <QStringList>

namespace speecher {

class DeliveryBackend {
public:
    virtual ~DeliveryBackend() = default;
    // A keyboard backend pastes what is already on the clipboard; a
    // clipboard backend copies content and says whether its HTML went too.
    // clearToInject is the final focus gate: a keyboard backend calls it after
    // all of its blocking preparation (modifier release) and immediately
    // before sending the paste keystroke, aborting when it returns false.
    // Clipboard-only backends may ignore it; a null function means nothing
    // gates the injection.
    virtual bool deliver(const DeliveryContent &content,
                         const std::function<bool()> &clearToInject,
                         bool *htmlAvailable,
                         QString *error = nullptr) = 0;
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
    DeliveryResult deliver(const OutputSettings &settings,
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

private:
    // The paste method deliver starts from, and whether it pastes blindly into
    // whatever has focus because the Target cannot be tracked.
    struct PasteChoice {
        PasteMethod method = PasteMethod::ClipboardOnly;
        bool currentFocusFallback = false;
    };
    class PreviousClipboard;

    void useDefaultBackendFactory();
    // The Paste Rule's method when the Target can be told apart from whatever
    // else is in front, or when a global rule pastes blindly into the current
    // focus; only the clipboard otherwise, and always for a secure field.
    static PasteChoice choosePasteMethod(const OutputSettings &settings, const Target &target);
    // Checks that the Target still has focus and readies it for the paste.
    // Returns why the paste keys must not go out, or nothing when they may.
    QString preparePasteInFocus(const Target &target);
    // Tries each backend for the paste method in turn. Nothing when none
    // delivered, with the first failure in firstError; the words are already
    // on the clipboard.
    std::optional<DeliveryResult> pasteThroughBackends(const OutputSettings &settings,
                                                       const DeliveryContent &content,
                                                       const Target &target,
                                                       const PasteChoice &choice,
                                                       bool copiedAsPlainText,
                                                       PreviousClipboard &previousClipboard,
                                                       QString *firstError);

    ClipboardDelivery m_clipboardDelivery;
    BackendFactory m_backendFactory;
    TargetProvider *m_targetProvider = nullptr;
};

} // namespace speecher
