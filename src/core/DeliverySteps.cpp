#include "core/DeliverySteps.h"

namespace speecher {

bool DeliverySteps::has(DeliveryStep step) const
{
    switch (step) {
    case DeliveryStep::Insert:
        return insert;
    case DeliveryStep::Press:
        return press;
    case DeliveryStep::Keep:
        break;
    }
    return keep;
}

DeliverySteps DeliverySteps::withStep(DeliveryStep step, bool on) const
{
    DeliverySteps next = *this;
    switch (step) {
    case DeliveryStep::Insert:
        next.insert = on;
        break;
    case DeliveryStep::Press:
        next.press = on;
        break;
    case DeliveryStep::Keep:
        next.keep = on;
        break;
    }
    return next.any() ? next : *this;
}

QStringList DeliveryPlan::route() const
{
    QStringList route;
    for (const DeliveryStep step : {DeliveryStep::Insert, DeliveryStep::Press, DeliveryStep::Keep}) {
        if (steps.has(step)) {
            route << deliveryStepName(step);
        }
    }
    return route;
}

QString deliveryStepName(DeliveryStep step)
{
    switch (step) {
    case DeliveryStep::Insert:
        return QStringLiteral("insert");
    case DeliveryStep::Press:
        return QStringLiteral("press");
    case DeliveryStep::Keep:
        break;
    }
    return QStringLiteral("keep");
}

DeliveryPlan deliveryPlan(const DeliverySteps &steps, PasteMethod method)
{
    DeliveryPlan plan{steps, method == PasteMethod::TerminalPaste};
    if (method == PasteMethod::DirectInsert || method == PasteMethod::ClipboardOnly) {
        plan.steps.press = false;
    }
    if (method == PasteMethod::ClipboardOnly) {
        plan.steps.insert = false;
    }
    return plan;
}

std::optional<DeliveryStep> stepRuleNeeds(const DeliverySteps &steps, PasteMethod method)
{
    const auto needs = [&steps](DeliveryStep step) -> std::optional<DeliveryStep> {
        return steps.has(step) ? std::nullopt : std::optional(step);
    };
    switch (method) {
    case PasteMethod::TerminalPaste:
        return needs(DeliveryStep::Press);
    case PasteMethod::DirectInsert:
        return needs(DeliveryStep::Insert);
    case PasteMethod::ClipboardOnly:
        return needs(DeliveryStep::Keep);
    case PasteMethod::StandardPaste:
        break;
    }
    return std::nullopt;
}

} // namespace speecher
