#pragma once

// The command-line client: sends a parsed command to the running Speecher
// over the local socket and prints its answer.

#include "app/CommandLine.h"

#include <memory>

namespace speecher {

class SingleInstancePlatform;

// Sends the decision's command to a running instance, or starts one detached
// when there is none. Needs a running QCoreApplication for the socket.
int runCliCommand(const CommandLineDecision &decision,
                  const std::shared_ptr<const SingleInstancePlatform> &platform);

} // namespace speecher
