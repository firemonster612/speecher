#pragma once

#include "core/ServiceOrder.h"

#include <QSettings>
#include <QString>

// How the speech and refinement Service Orders are stored: stt/services and
// refinement/services, each {"order": [...], "choices": {key: {"model",
// "language", "effort", "speed"}}}, empty fields left out. Nothing stored,
// or something that isn't an order, reads as the defaults, and a service the
// file has no choice for keeps its default one.

namespace speecher::ServiceOrderSettingsCodec {

ServiceOrder speech(const QSettings &settings);
// Another Local Model is a choice, which the hardware's suggestion no
// longer replaces (LocalSpeechSettings::modelChosen). An empty order saves
// as the default one.
void storeSpeech(QSettings &settings, const ServiceOrder &services);

// Until one is saved, the order starts with defaultRefinementProvider.
ServiceOrder refinement(const QSettings &settings);
// Saving the order read while none is saved leaves it a default, which the
// setup assistant may replace; only a different order is a choice.
void storeRefinement(QSettings &settings, const ServiceOrder &services);
// Saves services as the person's choice, even where it matches the default.
// An empty order saves as defaultRefinementProvider alone.
void storeChosenRefinement(QSettings &settings, const ServiceOrder &services);

// ChatGPT where the Codex app is installed, else Claude where Claude Code
// is, else ChatGPT.
QString defaultRefinementProvider();
// ChatGPT's and Claude's choices with a model, an effort and a speed each
// can run, fallbacks' where the stored one is missing or unknown.
ServiceOrder withValidRefinementChoices(ServiceOrder services,
                                        const ServiceOrder &fallbacks = defaultRefinementServices());
// Haiku 4.5, dated or not, has left the picker for Haiku 5.5; any other
// model as it is.
QString replacedAnthropicModel(const QString &model);

} // namespace speecher::ServiceOrderSettingsCodec
