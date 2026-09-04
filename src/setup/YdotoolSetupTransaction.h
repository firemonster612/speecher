#pragma once

#include <string>
#include <utility>
#include <vector>

namespace speecher {

class YdotoolSetupTransaction {
public:
    void record(std::string change)
    {
        m_changes.push_back(std::move(change));
    }

    void appendToError(std::string &error) const
    {
        if (m_changes.empty()) {
            return;
        }
        error += "\nChanges left behind:";
        for (const std::string &change : m_changes) {
            error += "\n- " + change;
        }
    }

private:
    std::vector<std::string> m_changes;
};

} // namespace speecher
