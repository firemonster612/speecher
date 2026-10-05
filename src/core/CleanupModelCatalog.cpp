#include "core/CleanupModelCatalog.h"

namespace speecher {
namespace {

const QList<CleanupModel> catalog{
    {QStringLiteral("LiquidAI/lfm2.5-1.2b-instruct"), QStringLiteral("LFM2.5 1.2B"), 730'000'000, 66},
    {QStringLiteral("gemma4:e4b"), QStringLiteral("Gemma 4 E4B"), 5'300'000'000, 81},
};
const CleanupModel &smallModel = catalog.at(0);
const CleanupModel &largeModel = catalog.at(1);

// Seconds for a 150-word dictation with the model loaded, from
// notes-llm.md's latency table: the ~1B column for the small model and, as
// the approved prototype does, the 3-4B column for Gemma 4 E4B. Rows: 8-core
// Zen 4, Radeon 780M, RTX 3060, M1, M3 Pro, M4 Max.
struct Latency {
    double small;
    double large;
};

Latency latencyFor(CleanupHardware hardware)
{
    switch (hardware) {
    case CleanupHardware::Cpu: return {2.5, 8.4};
    case CleanupHardware::IntegratedGpu: return {1.6, 5.3};
    case CleanupHardware::DedicatedGpu: return {0.4, 1.4};
    case CleanupHardware::AppleBase: return {2.3, 7.8};
    case CleanupHardware::ApplePro: return {1.1, 3.5};
    case CleanupHardware::AppleMax: break;
    }
    return {0.4, 1.3};
}

} // namespace

const QList<CleanupModel> &cleanupModelCatalog()
{
    return catalog;
}

CleanupHardware cleanupHardwareFor(const HardwareProfile &hardware)
{
    // Gemma 4 E4B is 5.3 GB before its context; a card that cannot hold it
    // spills to system memory and runs at the processor's pace.
    constexpr quint64 largeCleanupModelGpuBytes = quint64(8) << 30;
    switch (hardware.accelerator) {
    case HardwareProfile::Accelerator::Cpu:
        return CleanupHardware::Cpu;
    case HardwareProfile::Accelerator::IntegratedGpu:
        return CleanupHardware::IntegratedGpu;
    case HardwareProfile::Accelerator::DedicatedGpu:
        return hardware.gpuMemoryBytes >= largeCleanupModelGpuBytes ? CleanupHardware::DedicatedGpu
                                                                     : CleanupHardware::Cpu;
    case HardwareProfile::Accelerator::AppleSilicon:
        break;
    }
    if (hardware.chipName.contains(QStringLiteral("Max")) || hardware.chipName.contains(QStringLiteral("Ultra"))) {
        return CleanupHardware::AppleMax;
    }
    return hardware.chipName.contains(QStringLiteral("Pro")) ? CleanupHardware::ApplePro
                                                             : CleanupHardware::AppleBase;
}

double cleanupSeconds(const CleanupModel &model, CleanupHardware hardware)
{
    Q_ASSERT_X(model.ollamaTag == smallModel.ollamaTag || model.ollamaTag == largeModel.ollamaTag,
               "cleanupSeconds", qPrintable(model.ollamaTag));
    const Latency latency = latencyFor(hardware);
    return model.ollamaTag == largeModel.ollamaTag ? latency.large : latency.small;
}

std::optional<CleanupModel> suggestedCleanupModel(CleanupHardware hardware)
{
    if (cleanupSeconds(largeModel, hardware) <= 2.0) {
        return largeModel;
    }
    if (cleanupSeconds(smallModel, hardware) <= 3.0) {
        return smallModel;
    }
    return std::nullopt;
}

} // namespace speecher
