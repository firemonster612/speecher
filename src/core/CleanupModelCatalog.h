#pragma once

#include "core/LocalModelCatalog.h"

#include <QList>
#include <QString>

#include <optional>

namespace speecher {

// A cleanup model Speecher suggests pulling through Ollama for a Local Runner.
struct CleanupModel {
    QString ollamaTag;
    QString name;
    qint64 sizeBytes = 0;
    // Checks passed, of kRefinementBenchmarkChecks, on the refinement benchmark
    // in docs/provider-ratings-mockup/bench (15 transcripts x 3 runs through
    // the app's real prompts and default settings, 2026-10-05).
    int benchmarkChecksPassed = 0;
};

inline constexpr int kRefinementBenchmarkChecks = 123;

// LFM2.5 1.2B, then Gemma 4 E4B.
const QList<CleanupModel> &cleanupModelCatalog();

// The machine classes notes-llm.md's latency table has rows for.
enum class CleanupHardware {
    Cpu,
    IntegratedGpu,
    DedicatedGpu,
    AppleBase,
    ApplePro,
    AppleMax,
};

// The latency table's row for this machine. Apple chips are told apart by
// name, and a graphics card too small for the larger model counts as the
// processor, which is where that model would end up running.
CleanupHardware cleanupHardwareFor(const HardwareProfile &hardware);

// Seconds a catalog model takes to clean a 150-word dictation on this class
// of machine, with the model loaded.
double cleanupSeconds(const CleanupModel &model, CleanupHardware hardware);

// The larger model when it cleans a dictation in 2 s or less, the smaller
// when it takes 3 s or less, else nothing: cloud refinement or none will
// feel faster.
std::optional<CleanupModel> suggestedCleanupModel(CleanupHardware hardware);

} // namespace speecher
