#pragma once

namespace speecher {

// Routes the setup helpers' pkexec authentication prompts, which only appear
// when the session has no polkit agent, into a modal dialog on the GUI
// thread. Install once after QApplication exists.
void installLinuxAuthPrompt();

} // namespace speecher
