#pragma once

#include <QString>

#include <memory>

namespace speecher {

class ApplicationController;

namespace win {

class TranscribePane;

// The small window files opened from Explorer land in: the Transcribe pane and
// nothing else, no sidebar and no settings. It shares the pane with the
// settings window, so a batch started in either shows in both.
class TranscribeWindow {
public:
    // transcribe backs the window's content and outlives it.
    TranscribeWindow(ApplicationController *controller, TranscribePane *transcribe);
    ~TranscribeWindow();

    // Creates the window if none is open, otherwise brings it forward.
    void show();
    // Shows the window and saves a picture of it for --grab.
    bool capture(const QString &path);

private:
    struct Native;
    std::unique_ptr<Native> m_native;
};

} // namespace win
} // namespace speecher
