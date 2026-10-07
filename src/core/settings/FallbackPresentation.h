#pragma once

#include "core/ProviderChain.h"
#include "core/settings/SettingsSchema.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace speecher {

// Why a fallback can't stand in right now, judged only from what the app
// already knows (LiveFacts): nothing here refreshes a sign-in, asks a keyring
// or probes a server.
enum class FallbackProblem {
    None,
    Offline,
    SignedOut,
    NoModel,
    NoRunner,
    NoServer,
    // A speech fallback that can't listen for the Spoken Language is skipped.
    SpokenLanguage,
};

FallbackProblem fallbackProblem(ProviderRole role, const QString &providerId, const AppSettings &settings,
                                const LiveFacts &facts);

// What the primary's row says when the primary can't work right now, judged
// the same way, and which fallback takes over: "Can't reach ChatGPT right
// now. Dictation starts with Custom Endpoint." Empty while it can.
QString primaryProviderStatus(ProviderRole role, const AppSettings &settings, const LiveFacts &facts,
                              const QList<RowOption> &providers);

// One provider as `speecher providers` reports it, judged by fallbackProblem()
// from the same facts and worded as the primary's row words it.
struct ProviderReport {
    QString id;
    ProviderRole role = ProviderRole::Speech;
    // The registry label.
    QString label;
    // Holds what it needs from settings: a downloaded model, a server URL, a
    // chosen Local Runner.
    bool configured = false;
    // Empty for a provider that doesn't sign in, and for one whose sign-in
    // nothing has seen.
    std::optional<bool> signedIn;
    // Empty while the verdict turns on what nobody has checked: a sign-in not
    // seen, or whether the Local Runner is running.
    std::optional<bool> usable;
    // "Not signed in to ChatGPT."; empty while nothing known stops it.
    QString problem;
};

// Every provider `speechProviders` and `refinementProviders` hold, speech
// first, in their order.
QList<ProviderReport> providerReports(const AppSettings &settings, const LiveFacts &facts,
                                      const QList<RowOption> &speechProviders,
                                      const QList<RowOption> &refinementProviders);

// Whether a speech fallback is skipped because it can't listen for the
// Spoken Language, by its own language list or its Local Model's catalog
// entry (listensForSpokenLanguage). A missing model is not skipped here; it
// is unavailable. The primary is never skipped. A Dictation Session decides
// with this too.
bool fallbackSkipsSpokenLanguage(const SpeechSettings &speech, const QString &providerId);

// Whether a provider needs the internet to work: one that signs in, or a
// Custom Endpoint whose server isn't on this computer or its network.
bool needsInternet(ProviderRole role, const QString &providerId, const AppSettings &settings);

// Whether a provider works through an account the person signs in to (Claude
// Voice, ChatGPT Codex, OpenAI, Anthropic), rather than a key or this computer.
bool providerSignsIn(const QString &providerId);
// The account a provider signs in to, as a person knows it: "ChatGPT" for
// ChatGPT Codex. label is the provider's registry label.
QString signInName(const QString &providerId, const QString &label);

// What a provider a setting names but this build lacks is called.
QString providerNotInBuildLabel();

// Where a fallback list is edited. Setup offers no speech Custom Endpoint
// fallback: the Transcription step shows the endpoint's fields only while it
// is the chosen service, so a fallback added there could not be filled in.
enum class FallbackSurface { Settings, Setup };

// One fallback as its row shows it.
struct FallbackItem {
    QString providerId;
    // The registry label.
    QString label;
    // Which turn it gets, or why it can't stand in right now.
    QString status;
    StatusTone tone = StatusTone::Normal;
    bool canMoveUp = false;
    bool canMoveDown = false;
};

// The ordered list of a role's fallbacks, as the Fallbacks subpage and the
// setup assistant show it: a heading above the card, a subtitle in it, a row
// per fallback with Move up, Move down and Remove, an Add row, and a footer.
// Empty, with no items and no Add row, while refinement is None.
struct FallbackListPresentation {
    // "If ChatGPT Codex is unavailable".
    QString heading;
    QString subtitle;
    // Refinement's note that the raw transcript is pasted; empty for speech.
    QString footer;
    QList<FallbackItem> items;
    // The Add row, shown only while canAdd: the chain has room and a
    // provider is left to add.
    bool canAdd = false;
    QString addLabel;
    QString addHelp;
    // The combo's first entry, which chooses nothing.
    QString addPlaceholder;
    QList<RowOption> addChoices;
    // The tool buttons' captions and accessible names.
    QString moveUpCaption;
    QString moveDownCaption;
    QString removeCaption;
};

// `providers` are the role's providers this build offers, with their
// registry labels.
FallbackListPresentation fallbackListPresentation(ProviderRole role, const AppSettings &settings,
                                                  const LiveFacts &facts, const QList<RowOption> &providers,
                                                  FallbackSurface surface);

// What the Fallbacks button row says: the fallbacks in order, "Custom
// Endpoint, then Local Model", or what happens without any. When one can't
// stand in right now, the first such one's reason follows in the negative
// tone: "Local Model. No model downloaded, so it can't stand in yet."
struct FallbackSummary {
    QString text;
    StatusTone tone = StatusTone::Normal;
};
FallbackSummary fallbackSummary(ProviderRole role, const AppSettings &settings, const LiveFacts &facts,
                                const QList<RowOption> &providers);

// The role's fallbacks after one edit, normalized: ready to apply to the
// list row or to save. A front end edits through these so the ordering rules
// live here once.
QStringList withFallbackMoved(const AppSettings &settings, ProviderRole role, int index, int offset);
QStringList withFallbackRemoved(const AppSettings &settings, ProviderRole role, int index);
QStringList withFallbackAdded(const AppSettings &settings, ProviderRole role, const QString &providerId);

} // namespace speecher
