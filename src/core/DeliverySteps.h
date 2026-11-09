#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace speecher {

// The three ways a delivery gets words out, in the order it tries them, the
// paste keys Press presses, and the plan one delivery follows once its App's
// Pasting choice has had its say. The Pasting page, the Apps page, the setup
// assistant, search and TextDelivery all read this, so what the settings
// show is what a delivery does.
enum class DeliveryStep {
    // Writes the words into the focused field through accessibility.
    Insert,
    // Puts the words on the clipboard and presses the paste keys, when
    // insertion is off, unavailable or rejected. With Keep off it borrows
    // the clipboard and puts the previous one back once the paste is
    // confirmed or has had time to land, so Press alone pastes and leaves the
    // clipboard as it was.
    Press,
    // Leaves the words on the clipboard after delivery. Off, Speecher puts
    // the previous clipboard back once the paste is confirmed or has had time
    // to land, and copies nothing on its own.
    Keep,
};

// The keys Press presses: the usual paste keys (Ctrl+V, ⌘V on macOS), or a
// terminal's Ctrl+Shift+V.
enum class PasteKeyCombo {
    Standard,
    Terminal,
};

// Whether a terminal's paste keys differ from everyone else's. macOS presses
// ⌘V everywhere, so it has no choice of keys.
#ifdef Q_OS_MACOS
inline constexpr bool terminalKeysDiffer = false;
#else
inline constexpr bool terminalKeysDiffer = true;
#endif

// "standard" or "terminal", as settings store the keys; anything else reads
// as Standard.
QString pasteKeyComboName(PasteKeyCombo keys);
PasteKeyCombo pasteKeyComboFromName(const QString &name);

// Which steps the person has on. At least one is: setting the last one off
// is refused (withStep).
struct DeliverySteps {
    bool insert = true;
    bool press = true;
    bool keep = true;

    bool operator==(const DeliverySteps &other) const = default;
    bool has(DeliveryStep step) const;
    bool any() const { return insert || press || keep; }
    // These steps with step set to on, unless that would leave none on, in
    // which case they stay as they are.
    DeliverySteps withStep(DeliveryStep step, bool on) const;
};

// How an App or App Type gets its words out, its Pasting choice: steps of
// its own, paste keys of its own, either or both. A part left unset is the
// same as what the App or App Type follows, and in the end the Pasting
// page's steps and the usual paste keys.
struct PastingChoice {
    std::optional<DeliverySteps> steps;
    std::optional<PasteKeyCombo> keys;

    bool operator==(const PastingChoice &other) const = default;
    bool isSet() const { return steps || keys; }
};

// What one delivery does: its App's steps, or the Pasting page's, and the
// keys Press presses.
struct DeliveryPlan {
    DeliverySteps steps;
    PasteKeyCombo keys = PasteKeyCombo::Standard;

    bool operator==(const DeliveryPlan &other) const = default;
    // The steps that are on, in the order delivery tries them, by name.
    QStringList route() const;
};

// "insert", "press" or "keep", as settings rows and the UI name the steps.
QString deliveryStepName(DeliveryStep step);

// The plan for a Target whose App's Pasting choice, as resolveApp settles
// it, is pasting: its steps where it has some, else steps, and its keys.
// macOS presses ⌘V whichever keys it names.
DeliveryPlan deliveryPlan(const DeliverySteps &steps, const PastingChoice &pasting);

} // namespace speecher
