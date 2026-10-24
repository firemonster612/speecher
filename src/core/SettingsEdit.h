#pragma once

#include "core/AppSettings.h"

#include <QList>

#include <algorithm>
#include <functional>

// What the edits to the user's lists (WritingProfileEdits, VocabularyEdits)
// share: why one was refused, the change it makes to an entry, and the edit
// that undoes a removal.

namespace speecher {

// A change to a draft of the settings, such as the one that undoes a removal.
using SettingsEdit = std::function<void(AppSettings &)>;

// The fields an edit sets on a copy of an entry. Whatever it leaves alone
// keeps its stored value, such as a term's uses.
template <typename T>
using EntryChange = std::function<void(T &)>;

// Why an edit to one of the user's lists was refused.
enum class EditRefusal {
    // Nothing: the edit was saved.
    None,
    // The entry's name, term, phrase or app is empty.
    Blank,
    // Another entry of the list goes by that name already, ignoring case.
    Taken,
    // Something besides its name that the entry needs is missing: a tone's
    // instruction, a Custom only level's instructions, a replacement's text,
    // an app match's app type or profile, a correction's corrected text.
    Incomplete,
    // The entry edited is gone, or a tone, cleanup level, profile or service
    // it picks is not offered.
    Missing,
    // A built-in profile's name is its own.
    BuiltIn,
};

// Applies change to a copy of entry index of list and stores the copy when
// check, which may tidy it, refuses nothing. check takes the copy and its
// index; Missing for an index below 0.
template <typename T, typename Check>
EditRefusal changeEntry(QList<T> &list, qsizetype index, const EntryChange<T> &change, Check check)
{
    if (index < 0) {
        return EditRefusal::Missing;
    }
    T entry = list.at(index);
    change(entry);
    const EditRefusal refusal = check(entry, index);
    if (refusal == EditRefusal::None) {
        list[index] = entry;
    }
    return refusal;
}

// Appends entry to list when check, as for changeEntry with index -1,
// refuses nothing.
template <typename T, typename Check>
EditRefusal appendEntry(QList<T> &list, T entry, Check check)
{
    const EditRefusal refusal = check(entry, -1);
    if (refusal == EditRefusal::None) {
        list.append(entry);
    }
    return refusal;
}

// Takes entry index out of list. Returns what puts it back at its place in
// the list it is given, unless an entry `same` matches it is there by then:
// one added since keeps its values, and nothing is listed twice.
template <typename T, typename Same>
std::function<void(QList<T> &)> takeEntry(QList<T> &list, qsizetype index, Same same)
{
    const T removed = list.takeAt(index);
    return [index, removed, same](QList<T> &back) {
        if (std::none_of(back.cbegin(), back.cend(), [&](const T &entry) { return same(entry, removed); })) {
            back.insert(std::min(index, back.size()), removed);
        }
    };
}

} // namespace speecher
