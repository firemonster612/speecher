#pragma once

#include <QString>

namespace speecher {

class CliToolDiscovery {
public:
    static QString codexExecutable();
    static QString claudeCodeExecutable();

    static bool isCodexInstalled();
    static bool isClaudeCodeInstalled();
};

} // namespace speecher
