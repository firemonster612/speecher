#pragma once

#include "transcribe/FileTranscriptionSession.h"

#include <QStringList>

#include <functional>
#include <iosfwd>
#include <optional>

namespace speecher {

class AudioInput;
class ProviderRegistry;
class SettingsStore;

// The file name `speecher transcribe` reads stdin for; it is the only file.
inline const QString kStdinFile = QStringLiteral("-");

// The choices `speecher transcribe` takes on its command line. Each unset one
// comes from the user's settings, the way the Transcribe page seeds its form.
// `speecher listen` takes the same, without the ones about files.
struct HeadlessTranscribeOptions {
    std::optional<QString> speechProviderId;
    bool applyVocabulary = true;
    // From --vocab-file, for this run only.
    QStringList addedVocabulary;
    std::optional<QString> refinementProviderId;
    std::optional<QString> cleanupStrength;
    // Seeds cleanup and tone from the user's settings for this profile.
    std::optional<QString> writingProfile;
    std::optional<QString> tone;
    // Replaces the Spoken Language setting.
    std::optional<QString> spokenLanguage;
    TranscriptDestination destination = TranscriptDestination::BesideInput;
    QString folder;
    bool printTranscripts = false;
    // Print and save what the speech provider heard, not the refined text.
    bool raw = false;
    // Save and print subtitles instead of text. A file whose speech provider
    // returned no timings fails.
    TranscriptFormat format = TranscriptFormat::Text;
    // One JSON object per file on out, then a summary object.
    bool json = false;
};

// Transcribes files without a window, with its own providers, so it never
// touches a running instance's dictation. Files of just kStdinFile read the
// audio from in, through a temporary file named stdin that is removed when
// the run ends. Progress, saved paths and failures go to err (rewriting one
// line when err is a terminal), transcripts to out. Returns the exit code: 0
// when every file succeeded, 1 when any failed (saving, subtitles without
// timings and empty stdin included) or the session is busy, 2 for no files or
// a provider the registry does not offer. Each refusal is explained on err.
int runHeadlessTranscribe(const QStringList &files,
                          const HeadlessTranscribeOptions &options,
                          SettingsStore *settings,
                          ProviderRegistry *providers,
                          std::istream &in,
                          std::ostream &out,
                          std::ostream &err,
                          bool errIsTerminal);

// Records once from microphone, null when the user refused microphone access,
// until stopRequested() turns true, or, with
// untilSilenceMs, until that long passes without speech once speech was heard.
// It is polled, so it may be set from a signal handler; enterStops says
// whether pressing Enter sets it, for the hint. Then prints the
// transcript to out, or with options.json one object with it, and progress
// and failures to err. Like runHeadlessTranscribe it uses its own providers.
// Returns 0 when it printed a transcript, 1 when it failed (hearing no speech
// included), 2 for a provider the registry does not offer.
int runHeadlessListen(const HeadlessTranscribeOptions &options,
                      std::optional<int> untilSilenceMs,
                      AudioInput *microphone,
                      const std::function<bool()> &stopRequested,
                      bool enterStops,
                      SettingsStore *settings,
                      ProviderRegistry *providers,
                      std::ostream &out,
                      std::ostream &err);

} // namespace speecher
