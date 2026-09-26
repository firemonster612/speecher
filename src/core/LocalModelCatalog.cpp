#include "core/LocalModelCatalog.h"

#include <algorithm>

namespace speecher {
namespace {

constexpr quint64 gib = quint64(1) << 30;
// A loaded model takes about a third more memory than its file.
constexpr double runtimeMemoryFactor = 1.35;
constexpr double maxSuggestedSecondsFor10s = 1.5;

// Q8_0 files unless noted. Revisions and sha256 are the Hugging Face commit and
// LFS object checked on 2026-09-25; WER and speed are from transcribe.cpp
// v0.2.4's catalog/<variant>.json (speed: xrt_compute on the 35 s dots
// sample). Granite Speech 5.0 470M TurboCTC is left out: its output has no
// punctuation or capitals.
const QList<LocalModel> catalog{
    {
        QStringLiteral("moonshine-small"),
        QStringLiteral("Moonshine Small"),
        QStringLiteral("handy-computer/moonshine-streaming-small-gguf"),
        QStringLiteral("7e32b1b3dfce5d3a38dad59630ffce608f15c4aa"),
        QStringLiteral("moonshine-streaming-small-Q8_0.gguf"),
        QStringLiteral("d03670f69629b649085d0f44a63d97668b4119117cc9611a4e4ad94341713dfc"),
        198506848,
        2.54,
        8.55,
        true,
        QStringLiteral("MIT"),
        58.59,
        14.16,
        {QStringLiteral("Smallest download"),
         QStringLiteral("Words appear as you speak"),
         QStringLiteral("Quickest on older processors")},
        {QStringLiteral("About twice the mistakes of Parakeet on everyday speech")},
    },
    {
        QStringLiteral("parakeet"),
        QStringLiteral("Parakeet 0.6B"),
        QStringLiteral("handy-computer/parakeet-unified-en-0.6b-gguf"),
        QStringLiteral("d5249700b2382bf5c5024c2421d101b8db54a629"),
        QStringLiteral("parakeet-unified-en-0.6b-Q8_0.gguf"),
        QStringLiteral("4b50b6dd862bf6e346929aaf4f5eaacec003bfa3f56462d6c874b41ef2f38795"),
        731357568,
        1.60,
        3.99,
        true,
        // transcribe.cpp's catalog says CC-BY-4.0; the upstream model card wins.
        QStringLiteral("NVIDIA Open Model License"),
        228.39,
        26.01,
        {QStringLiteral("Words appear as you speak"),
         QStringLiteral("Fewest mistakes on everyday speech of the small models")},
        {QStringLiteral("731 MB download")},
    },
    {
        QStringLiteral("cohere"),
        QStringLiteral("Cohere Transcribe"),
        QStringLiteral("handy-computer/cohere-transcribe-03-2026-gguf"),
        QStringLiteral("ab667acedcb5d8d56837ef3ed3568c3ad50dde47"),
        QStringLiteral("cohere-transcribe-03-2026-Q8_0.gguf"),
        QStringLiteral("931916663432fd895423a4291a8400221802b288967ca2d435fc5e3141c9e71e"),
        2410655232,
        1.27,
        5.08,
        false,
        QStringLiteral("Apache 2.0"),
        75.14,
        8.52,
        {QStringLiteral("Fewest mistakes on clear speech"),
         QStringLiteral("Top open model on the Open ASR leaderboard")},
        {QStringLiteral("2.4 GB download"),
         QStringLiteral("Text appears after you stop"),
         QStringLiteral("Slow without a dedicated GPU")},
    },
    {
        // Q5_K_M: Q4_K_M loses half a point of WER and would be no better than
        // Cohere. Clear-speech WER is the Q5_K_M figure; FLEURS was measured
        // at Q8_0 only. Speed is the Q8_0 run, the nearest quant measured;
        // transcribe.cpp did not run it on the Ryzen 4750U.
        QStringLiteral("voxtral-small"),
        QStringLiteral("Voxtral Small 24B"),
        QStringLiteral("handy-computer/Voxtral-Small-24B-2507-gguf"),
        QStringLiteral("4ab5a0708a16619b2b5b98f14b20333e0de17943"),
        QStringLiteral("Voxtral-Small-24B-2507-Q5_K_M.gguf"),
        QStringLiteral("a53f73a5f63b7663fe155977616a636acac567dac898f6ef82ca519017e8b6e7"),
        17138659808,
        1.60,
        3.55,
        false,
        QStringLiteral("Apache 2.0"),
        2.41,
        0,
        {QStringLiteral("Fewest mistakes on everyday speech"),
         QStringLiteral("Most accurate open model on real-world recordings")},
        {QStringLiteral("17 GB download"),
         QStringLiteral("Text appears after you stop"),
         QStringLiteral("Needs a 24 GB graphics card or a Mac with 32 GB or more"),
         QStringLiteral("Slow: several seconds after you stop")},
    },
};

quint64 minusOrZero(quint64 value, quint64 subtract)
{
    return value > subtract ? value - subtract : 0;
}

quint64 memoryNeedBytes(const LocalModel &model)
{
    return quint64(double(model.sizeBytes) * runtimeMemoryFactor);
}

} // namespace

const QList<LocalModel> &localModelCatalog()
{
    return catalog;
}

const LocalModel *findLocalModel(const QString &id)
{
    const auto it = std::find_if(catalog.cbegin(), catalog.cend(),
                                 [&id](const LocalModel &model) { return model.id == id; });
    return it == catalog.cend() ? nullptr : &*it;
}

quint64 memoryBudgetBytes(const HardwareProfile &hardware)
{
    switch (hardware.accelerator) {
    case HardwareProfile::Accelerator::DedicatedGpu:
        return minusOrZero(hardware.gpuMemoryBytes, gib);
    case HardwareProfile::Accelerator::AppleSilicon: {
        // What Jan observed macOS leaves an app: RAM less 10% and 2.5 GB.
        const quint64 usable =
            minusOrZero(quint64(double(hardware.systemRamBytes) * 0.9), quint64(2.5 * double(gib)));
        return std::min(hardware.gpuMemoryBytes, quint64(0.85 * double(usable)));
    }
    case HardwareProfile::Accelerator::IntegratedGpu:
    case HardwareProfile::Accelerator::Cpu:
        break;
    }
    // An integrated GPU's memory is system RAM, so it never adds to the budget.
    return minusOrZero(hardware.availableRamBytes, 2 * gib);
}

ModelFit modelFit(const LocalModel &model, const HardwareProfile &hardware)
{
    const quint64 need = memoryNeedBytes(model);
    const quint64 budget = memoryBudgetBytes(hardware);
    if (need <= budget / 2) {
        return ModelFit::Fits;
    }
    return need <= budget ? ModelFit::Tight : ModelFit::TooLarge;
}

std::optional<SpeedEstimate> estimatedSpeed(const LocalModel &model, const HardwareProfile &hardware)
{
    const bool apple = hardware.accelerator == HardwareProfile::Accelerator::AppleSilicon;
    const double multiple = apple ? model.m4MaxMetalSpeed : model.ryzen4750uVulkanSpeed;
    if (multiple <= 0) {
        return std::nullopt;
    }
    const QString referenceChip = apple ? QStringLiteral("M4 Max") : QStringLiteral("4750U");
    return SpeedEstimate{10.0 / multiple, hardware.chipName.contains(referenceChip)};
}

const LocalModel &suggestedLocalModel(const HardwareProfile &hardware)
{
    const auto quickStreamingFit = [&](const LocalModel &model) {
        if (!model.streams || modelFit(model, hardware) != ModelFit::Fits) {
            return false;
        }
        const std::optional<SpeedEstimate> speed = estimatedSpeed(model, hardware);
        return speed && speed->secondsFor10sSpeech <= maxSuggestedSecondsFor10s;
    };
    const LocalModel *best = nullptr;
    for (const LocalModel &model : catalog) {
        if (quickStreamingFit(model) && (!best || model.fleursEnglishWer < best->fleursEnglishWer)) {
            best = &model;
        }
    }
    if (best) {
        return *best;
    }
    // Fit depends only on size, so the smallest model is also the smallest
    // that fits whenever any does.
    return *std::min_element(catalog.cbegin(), catalog.cend(),
                             [](const LocalModel &left, const LocalModel &right) {
                                 return left.sizeBytes < right.sizeBytes;
                             });
}

} // namespace speecher
