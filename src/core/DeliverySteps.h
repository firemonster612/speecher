#pragma once

#include "core/PasteMethod.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace speecher {

// The three ways a delivery gets words out, in the order it tries them, and
// the plan one delivery follows once its Paste Rule has narrowed them. The
// Pasting page, the setup assistant, search and TextDelivery all read this,
// so what the settings show is what a delivery does.
enum class DeliveryStep {
    // Writes the words into the focused field through accessibility.
    Insert,
    // Puts the words on the clipboard and presses the paste keys, when
    // insertion is off, unavailable or rejected.
    Press,
    // Leaves the words on the clipboard after delivery. Off, Speecher puts
    // the previous clipboard back after a confirmed paste and copies nothing
    // on its own.
    Keep,
};

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

// What one delivery does: the steps the settings allow after the winning
// Paste Rule narrowed them, and which paste keys Press uses. A rule never
// turns on a step the settings have off.
struct DeliveryPlan {
    DeliverySteps steps;
    // Ctrl+Shift+V, a terminal's paste keys, rather than Ctrl+V. macOS
    // presses ⌘V either way.
    bool terminalKeys = false;

    bool operator==(const DeliveryPlan &other) const = default;
    // The steps that are on, in the order delivery tries them, by name.
    QStringList route() const;
};

// "insert", "press" or "keep", as settings rows and the UI name the steps.
QString deliveryStepName(DeliveryStep step);

// The plan for a Target whose winning rule says method: Standard paste
// takes the steps as they are, Terminal paste the same with the terminal's
// keys, Direct insertion skips Press, and Clipboard only skips Insert and
// Press.
DeliveryPlan deliveryPlan(const DeliverySteps &steps, PasteMethod method);

// The step a rule with this method relies on that steps leave off, so the
// rule can't do what it says: Terminal paste needs Press, Clipboard only
// needs Keep. Nothing for Standard paste, which only follows the steps, nor
// for Direct insertion, which only takes Press away.
std::optional<DeliveryStep> stepRuleNeeds(const DeliverySteps &steps, PasteMethod method);

} // namespace speecher
