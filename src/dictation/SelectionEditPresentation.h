#pragma once

#include <QList>
#include <QMetaType>
#include <QString>

namespace speecher {

// One stretch of a selection edit as the review shows it. A front end marks
// text by its kind and shows trailing after it unmarked, so the marks on two
// neighbouring words never run together.
struct EditRun {
    enum class Kind {
        Kept,
        Removed,
        Added,
        // Unchanged words a folded review leaves out; its text is "…".
        Omitted,
    };
    Kind kind = Kind::Kept;
    QString text;
    QString trailing;

    bool operator==(const EditRun &other) const = default;
};

// The keys that keep the original and replace it on this desktop, as the
// review's buttons name them; empty where no key does.
struct ReviewKeys {
    QString keep;
    QString replace;

    bool operator==(const ReviewKeys &other) const = default;
};

// A selection edit waiting for Replace or Keep original: the revised
// selection against the original, word by word.
struct SelectionEditReview {
    // What was said, quoted.
    QString instruction;
    // The whole edit. When most words changed, a diff would be noise, so a
    // rewrite is the revised text alone, kept.
    QList<EditRun> runs;
    // The changed stretches with a few words either side, the unchanged words
    // between them Omitted. Empty when the whole edit is short enough to show.
    QList<EditRun> folded;
    // "2 changes", "Rewritten", "Spacing changes only" or "No changes".
    QString summary;
    ReviewKeys keys;
};

SelectionEditReview selectionEditReview(const QString &original,
                                        const QString &revised,
                                        const QString &instruction,
                                        const ReviewKeys &keys);

// The review's captions.
QString keepOriginalCaption();
QString replaceSelectionCaption();
QString showWholeEditCaption();
QString showChangesOnlyCaption();
// The keys as the review names them.
QString escapeKeyName();
QString enterKeyName();
// The review's key names from the keys a session holds: Escape, or else the
// Cancel Shortcut (its display text, empty when unbound), keeps; Enter
// replaces.
ReviewKeys reviewKeysFor(bool escapeHeld, bool enterHeld, const QString &cancelShortcut);
// Why an edit went to the clipboard instead of into the Target, after the
// receipt that says it was copied.
QString selectionChangedNote();

} // namespace speecher

Q_DECLARE_METATYPE(speecher::SelectionEditReview)
