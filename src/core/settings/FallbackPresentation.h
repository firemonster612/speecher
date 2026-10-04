#pragma once

#include "core/ProviderChain.h"
#include "core/settings/SettingsSchema.h"

#include <QList>
#include <QString>
#include <QStringList>

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
    // A Local Model that can't listen for the Spoken Language is skipped.
    SpokenLanguage,
};

FallbackProblem fallbackProblem(ProviderRole role, const QString &providerId, const AppSettings &settings,
                                const LiveFacts &facts);

// What the primary's row says when the primary can't work right now, judged
// the same way, and which fallback takes over: "Can't reach ChatGPT right
// now. Dictation starts with Custom Endpoint." Empty while it can.
QString primaryProviderStatus(ProviderRole role, const AppSettings &settings, const LiveFacts &facts,
                              const QList<RowOption> &providers);

// Whether a speech fallback is skipped because it can't listen for the
// Spoken Language: only a Local Model whose catalog entry lacks it. A
// missing model is not skipped here; it is unavailable. The primary is never
// skipped.
bool fallbackSkipsSpokenLanguage(const SpeechSettings &speech, const QString &providerId);

// Whether a provider works through an account the person signs in to (Claude
// Voice, ChatGPT Codex, OpenAI, Anthropic), rather than a key or this computer.
bool providerSignsIn(const QString &providerId);
// The account a provider signs in to, as a person knows it: "ChatGPT" for
// ChatGPT Codex. label is the provider's registry label.
QString signInName(const QString &providerId, const QString &label);

// Where a fallback list is edited. Setup offers no speech Custom Endpoint,
// which is set up in Settings only.
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
// Endpoint, then Local Model", or what happens without any.
QString fallbackSummary(ProviderRole role, const AppSettings &settings, const QList<RowOption> &providers);

// The role's fallbacks after one edit, normalized: ready to apply to the
// list row or to save. A front end edits through these so the ordering rules
// live here once.
QStringList withFallbackMoved(const AppSettings &settings, ProviderRole role, int index, int offset);
QStringList withFallbackRemoved(const AppSettings &settings, ProviderRole role, int index);
QStringList withFallbackAdded(const AppSettings &settings, ProviderRole role, const QString &providerId);

} // namespace speecher
