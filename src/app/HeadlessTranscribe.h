#pragma once

#include "transcribe/FileTranscriptionSession.h"

#include <QStringList>

#include <iosfwd>
#include <optional>

namespace speecher {

class ProviderRegistry;
class SettingsStore;

// The choices `speecher transcribe` takes on its command line. Each unset one
// comes from the user's settings, the way the Transcribe page seeds its form.
struct HeadlessTranscribeOptions {
    std::optional<QString> speechProviderId;
    bool applyVocabulary = true;
    std::optional<QString> refinementProviderId;
    std::optional<QString> cleanupStrength;
    // Seeds cleanup and tone from the user's settings for this profile.
    std::optional<QString> writingProfile;
    std::optional<QString> tone;
    TranscriptDestination destination = TranscriptDestination::BesideInput;
    QString folder;
    bool printTranscripts = false;
    // Print and save what the speech provider heard, not the refined text.
    bool raw = false;
    // One JSON object per file on out, then a summary object.
    bool json = false;
};

// Transcribes files without a window, with its own providers, so it never
// touches a running instance's dictation. Progress, saved paths and failures
// go to err (rewriting one line when err is a terminal), transcripts to out.
// Returns the exit code: 0 when every file succeeded, 1 when any failed, 2
// for a provider the registry does not offer.
int runHeadlessTranscribe(const QStringList &files,
                          const HeadlessTranscribeOptions &options,
                          SettingsStore *settings,
                          ProviderRegistry *providers,
                          std::ostream &out,
                          std::ostream &err,
                          bool errIsTerminal);

} // namespace speecher
