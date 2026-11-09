#pragma once

#include "core/AppSettings.h"
#include "core/BindingProcessor.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QStringList>

// What one dictation takes from the vocabulary: the one test of whether an
// entry applies where it runs, and the three lists built with it, the speech
// service's hints, refinement's terms and the replacements.

namespace speecher {

// Where a dictation runs: its Writing Profile, and its Target with the
// person's Apps, which an entry's "app:" limits name. An empty profile is a
// request made before one is known, which every entry that is on applies to.
struct VocabularyScope {
    QString writingProfile;
    Target target;
    AppsSettings apps;
};

// Whether the entry is on and its limits hold here: no profiles or this
// one among them, and no apps or one of them matching the Target. An App
// limit matches as that App does (appMatches); an application id, a Learned
// Correction's, matches the Target's application id ignoring case.
bool vocabularyEntryApplies(const VocabularyEntry &entry, const VocabularyScope &scope);
// Whether one of an entry's app limits matches the Target, as
// vocabularyEntryApplies tests each.
bool appLimitMatches(const QString &limit, const Target &target, const AppsSettings &apps);

// The terms a speech request carries: `leadingTerms` in their order, then
// the key spellings that apply, then what the Learned Corrections that apply
// write, unless a spelling lists it, each term once and within the caps.
QStringList speechVocabulary(const QList<VocabularyEntry> &entries,
                             const VocabularyScope &scope,
                             const QStringList &leadingTerms = {});

// What refinement reads as vocabulary: what the Learned Corrections that
// apply write, first, since each came from a real edit and neither a long
// run's terms nor a full list may push it out; then `runTerms` in their
// order; then every spelling that applies, in priority order, not the speech
// request's capped list. Each term once, up to VocabularyLimit's refinement
// ceiling. The spellings' contexts go into `context`, keyed by the spelling
// kept.
QStringList refinementVocabulary(const QList<VocabularyEntry> &entries,
                                 const VocabularyScope &scope,
                                 const QStringList &runTerms,
                                 QHash<QString, QString> &context);

// The replacements that apply, as the dictation's binding rules: the
// person's own before Learned Corrections, and within each those limited to
// apps, then to profiles, then the rest, each phrase once (as normalized), so
// the narrower one wins. A {date} or {time} in the person's own becomes
// `now`'s date or time in the locale's short format; a Learned Correction's
// text is what the person typed, braces and all. A Learned Correction that
// writes a spelling limited to other profiles stays out with it.
QList<BindingRule> replacementRules(const QList<VocabularyEntry> &entries,
                                    const VocabularyScope &scope,
                                    const QDateTime &now);

} // namespace speecher
