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

QString pasteKeyComboName(PasteKeyCombo keys)
{
    return keys == PasteKeyCombo::Terminal ? QStringLiteral("terminal") : QStringLiteral("standard");
}

PasteKeyCombo pasteKeyComboFromName(const QString &name)
{
    return name == u"terminal" ? PasteKeyCombo::Terminal : PasteKeyCombo::Standard;
}

DeliveryPlan deliveryPlan(const DeliverySteps &steps, const PastingChoice &pasting)
{
    return {pasting.steps.value_or(steps), pasting.keys.value_or(PasteKeyCombo::Standard)};
}

} // namespace speecher
