#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace speecher {

// What a privileged install has changed so far, and how to take each change
// back. A failed install must not leave root-made changes the app has no
// button to remove, so the helper rolls the transaction back and reports what
// it could and could not undo.
class YdotoolSetupTransaction {
public:
    using Undo = std::function<bool()>;

    // A change with no undo is one that is not safe to reverse: a package the
    // distribution installed, a kernel module something else may now be using.
    // It is recorded so the report names it, not so it can be taken back.
    void record(std::string change, Undo undo = {})
    {
        m_changes.push_back({std::move(change), std::move(undo)});
    }

    // Newest change first, and never stops early: one undo that fails must not
    // strand the ones after it.
    void rollBack()
    {
        for (auto change = m_changes.rbegin(); change != m_changes.rend(); ++change) {
            if (change->undo && change->undo()) {
                m_undone.push_back(change->description);
            } else {
                m_remaining.push_back(change->description);
            }
        }
        m_changes.clear();
    }

    // Called after rollBack(), or on its own by a helper that does not roll
    // back, in which case every recorded change is still on the machine.
    void appendToError(std::string &error) const
    {
        std::vector<std::string> left = m_remaining;
        for (const Change &change : m_changes) {
            left.push_back(change.description);
        }
        appendList(error, "Changes undone:", m_undone);
        appendList(error, "Changes left behind:", left);
    }

private:
    struct Change {
        std::string description;
        Undo undo;
    };

    static void appendList(std::string &error,
                           const char *heading,
                           const std::vector<std::string> &items)
    {
        if (items.empty()) {
            return;
        }
        error += '\n';
        error += heading;
        for (const std::string &item : items) {
            error += "\n- " + item;
        }
    }

    std::vector<Change> m_changes;
    std::vector<std::string> m_undone;
    std::vector<std::string> m_remaining;
};

} // namespace speecher
