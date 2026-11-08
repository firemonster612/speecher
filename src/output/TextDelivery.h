#pragma once

#include "dictation/DictationPorts.h"
#include "output/ClipboardDelivery.h"

#include <functional>
#include <memory>
#include <QPointer>
#include <QString>

namespace speecher {

class DeliveryBackend {
public:
    struct Outcome {
        bool ok = false;
        // A failure the backend knows sent no key, such as a keystroke it
        // could not create or a batch Windows refused whole: the clipboard
        // the paste borrowed goes back. Otherwise a failure after the gate
        // let the keys go may have sent some, and the words stay copied.
        bool noInputSent = false;
        QString error;
    };
    // Asked with a reply, which hears whether the keystroke may go out.
    using PasteGate = std::function<void(std::function<void(bool clear)>)>;
    virtual ~DeliveryBackend() = default;
    // A backend presses the paste keys, pasting what is already on the
    // clipboard. done runs once with the outcome, and may run before
    // deliver() returns.
    // clearToInject is the final gate: a keyboard backend asks it after all
    // of its preparation (modifier release) and sends the paste keystroke
    // from its reply, aborting when the reply is false. It refuses once the
    // delivery was answered, and otherwise when focus moved off the Target;
    // asking the compositor, it may reply on a later turn of the event loop.
    virtual void deliver(const DeliveryContent &content,
                         const PasteGate &clearToInject,
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
    ~TextDelivery() override;
    void deliver(const OutputSettings &settings,
                 const DeliveryContent &content,
                 const Target &target) override;
    // The paste keys backend the Press step uses here, as the backend
    // factory is asked for it, or empty where none can press them yet (Linux
    // before the virtual keyboard is set up).
    static QString pressMethod(const OutputSettings &settings);
    // How long a delivery may take before it is answered as unfinished and
    // its remaining steps dropped. Tests shorten it.
    void setAnswerDeadlineMs(int ms);

private:
    // The plan deliver follows, and whether it pastes blindly into whatever
    // has focus because the Target cannot be tracked.
    struct PlanChoice {
        DeliveryPlan plan;
        bool currentFocusFallback = false;
    };
    class PreviousClipboard;
    // One delivery's steps, from the plan to the answer.
    class Run;

    void useDefaultBackendFactory();
    // The steps narrowed by the Paste Rule when the Target can be told apart
    // from whatever else is in front, or when the steps alone press the keys
    // blindly into the current focus; only Keep otherwise, and always for a
    // secure field.
    static PlanChoice choosePlan(const OutputSettings &settings, const Target &target);
    // Runs the first request, unless a run is still there, answered or not.
    void startNext();
    // Answers the running request, once. The next starts once its run is gone.
    void answer(const DeliveryResult &result);

    struct Request {
        OutputSettings settings;
        DeliveryContent content;
        Target target;
    };
    // The delivery under way first, then those asked for meanwhile.
    QList<Request> m_requests;
    // From its start until it is destroyed, which can be well after its answer.
    QPointer<Run> m_run;
    // Longer than every step's own time limit put together, so only a
    // delivery that is stuck runs into it.
    int m_answerDeadlineMs = 30000;
    ClipboardDelivery m_clipboardDelivery;
    BackendFactory m_backendFactory;
    TargetProvider *m_targetProvider = nullptr;
};

} // namespace speecher
