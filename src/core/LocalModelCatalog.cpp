#include "core/LocalModelCatalog.h"

#include <algorithm>

namespace speecher {
namespace {

constexpr quint64 gib = quint64(1) << 30;
// A loaded model takes about a third more memory than its file.
constexpr double runtimeMemoryFactor = 1.35;
constexpr double maxSuggestedSecondsFor10s = 1.5;

// Q8_0 files unless noted. Revisions and sha256 are the Hugging Face commit and
// LFS object checked on 2026-09-25 or later; WER and speed are from transcribe.cpp
// v0.2.4's catalog/<variant>.json (speed: xrt_compute on the 35 s dots
// sample). Granite Speech 5.0 470M TurboCTC is left out: its output has no
// punctuation or capitals.
const QList<LocalModel> catalog{
    {
        .id = QStringLiteral("moonshine-small"),
        .name = QStringLiteral("Moonshine Small"),
        .huggingFaceRepo = QStringLiteral("handy-computer/moonshine-streaming-small-gguf"),
        .revision = QStringLiteral("7e32b1b3dfce5d3a38dad59630ffce608f15c4aa"),
        .fileName = QStringLiteral("moonshine-streaming-small-Q8_0.gguf"),
        .sha256 = QStringLiteral("d03670f69629b649085d0f44a63d97668b4119117cc9611a4e4ad94341713dfc"),
        .sizeBytes = 198506848,
        .librispeechCleanWer = 2.54,
        .fleursEnglishWer = 8.55,
        .streams = true,
        .licence = QStringLiteral("MIT"),
        .m4MaxMetalSpeed = 58.59,
        .ryzen4750uVulkanSpeed = 14.16,
        .rating = ModelRating::Good,
        .bestFor = QStringLiteral("Computers short on memory"),
        .pros = {QStringLiteral("Smallest download"),
                 QStringLiteral("Words appear as you speak")},
        .cons = {QStringLiteral("About twice the mistakes of Parakeet on everyday speech")},
    },
    {
        .id = QStringLiteral("moonshine-medium"),
        .name = QStringLiteral("Moonshine Medium"),
        .huggingFaceRepo = QStringLiteral("handy-computer/moonshine-streaming-medium-gguf"),
        .revision = QStringLiteral("0f99e956a9e63d591ddd7f2a20dfead255e96c68"),
        .fileName = QStringLiteral("moonshine-streaming-medium-Q8_0.gguf"),
        .sha256 = QStringLiteral("f7c9564249b508f6012927ec4f9e536087da53a7047f858ca9975bea5f75299e"),
        .sizeBytes = 295793568,
        .librispeechCleanWer = 2.16,
        .fleursEnglishWer = 7.87,
        .streams = true,
        .licence = QStringLiteral("MIT"),
        .m4MaxMetalSpeed = 36.12,
        .ryzen4750uVulkanSpeed = 8.9,
        .rating = ModelRating::Good,
        .bestFor = QStringLiteral("Computers short on memory"),
        .pros = {QStringLiteral("Words appear as you speak"),
                 QStringLiteral("Fewer mistakes than Moonshine Small")},
        .cons = {QStringLiteral("About twice the mistakes of Parakeet on everyday speech")},
    },
    {
        .id = QStringLiteral("parakeet"),
        .name = QStringLiteral("Parakeet 0.6B"),
        .huggingFaceRepo = QStringLiteral("handy-computer/parakeet-unified-en-0.6b-gguf"),
        .revision = QStringLiteral("d5249700b2382bf5c5024c2421d101b8db54a629"),
        .fileName = QStringLiteral("parakeet-unified-en-0.6b-Q8_0.gguf"),
        .sha256 = QStringLiteral("4b50b6dd862bf6e346929aaf4f5eaacec003bfa3f56462d6c874b41ef2f38795"),
        .sizeBytes = 731357568,
        .librispeechCleanWer = 1.60,
        .fleursEnglishWer = 3.99,
        .streams = true,
        // transcribe.cpp's catalog says CC-BY-4.0; the upstream model card wins.
        .licence = QStringLiteral("NVIDIA Open Model License"),
        .m4MaxMetalSpeed = 228.39,
        .ryzen4750uVulkanSpeed = 26.01,
        .rating = ModelRating::Recommended,
        .bestFor = QStringLiteral("Most people"),
        .pros = {QStringLiteral("Words appear as you speak"),
                 QStringLiteral("Fewest mistakes of the models that show words as you speak"),
                 QStringLiteral("Quick even on older laptops")},
        .cons = {QStringLiteral("More mistakes than Qwen3-ASR on everyday speech")},
    },
    {
        .id = QStringLiteral("whisper-turbo"),
        .name = QStringLiteral("Whisper Large v3 Turbo"),
        .huggingFaceRepo = QStringLiteral("handy-computer/whisper-large-v3-turbo-gguf"),
        .revision = QStringLiteral("ceea6c8a94a21ab85be244d311e874a39344dbf5"),
        .fileName = QStringLiteral("whisper-large-v3-turbo-Q8_0.gguf"),
        .sha256 = QStringLiteral("b2e30cc286bc9f3aba4db9099fc7403543497c05ce7100d0d83091ddfd25a183"),
        .sizeBytes = 886381760,
        .librispeechCleanWer = 2.01,
        .fleursEnglishWer = 4.38,
        .streams = false,
        .licence = QStringLiteral("Apache 2.0"),
        .m4MaxMetalSpeed = 51.12,
        .ryzen4750uVulkanSpeed = 3.69,
        .rating = ModelRating::Situational,
        .bestFor = QStringLiteral("Few mistakes in under 1 GB, if you can wait"),
        .pros = {QStringLiteral("Fewer mistakes than the Moonshine models on everyday speech")},
        .cons = {QStringLiteral("Text appears after you stop"),
                 QStringLiteral("More mistakes than Parakeet on everyday speech"),
                 QStringLiteral("Now and then leaves out punctuation"),
                 QStringLiteral("A few seconds' wait on older laptops")},
    },
    {
        // Hard-capped rather than chunked, at over an hour of audio.
        .id = QStringLiteral("qwen3-asr"),
        .name = QStringLiteral("Qwen3-ASR 1.7B"),
        .huggingFaceRepo = QStringLiteral("handy-computer/Qwen3-ASR-1.7B-gguf"),
        .revision = QStringLiteral("3555bd238a8572bbace3ebf60d23b036dc0a5dbe"),
        .fileName = QStringLiteral("Qwen3-ASR-1.7B-Q8_0.gguf"),
        .sha256 = QStringLiteral("9a0d81792dfea2d5f278b8a63deb3ea6e02139ce42c2301f32ea19c4f77526b7"),
        .sizeBytes = 2185030624,
        .librispeechCleanWer = 1.62,
        .fleursEnglishWer = 3.23,
        .streams = false,
        .licence = QStringLiteral("Apache 2.0"),
        .m4MaxMetalSpeed = 36.83,
        .ryzen4750uVulkanSpeed = 3.72,
        .rating = ModelRating::Good,
        .bestFor = QStringLiteral("Fewest mistakes, if you can wait"),
        .pros = {QStringLiteral("Fewest mistakes on everyday speech")},
        .cons = {QStringLiteral("2.2 GB download"),
                 QStringLiteral("Text appears after you stop"),
                 QStringLiteral("A few seconds' wait on older laptops")},
    },
    {
        .id = QStringLiteral("cohere"),
        .name = QStringLiteral("Cohere Transcribe"),
        .huggingFaceRepo = QStringLiteral("handy-computer/cohere-transcribe-03-2026-gguf"),
        .revision = QStringLiteral("ab667acedcb5d8d56837ef3ed3568c3ad50dde47"),
        .fileName = QStringLiteral("cohere-transcribe-03-2026-Q8_0.gguf"),
        .sha256 = QStringLiteral("931916663432fd895423a4291a8400221802b288967ca2d435fc5e3141c9e71e"),
        .sizeBytes = 2410655232,
        .librispeechCleanWer = 1.27,
        .fleursEnglishWer = 5.08,
        .streams = false,
        .licence = QStringLiteral("Apache 2.0"),
        .m4MaxMetalSpeed = 75.14,
        .ryzen4750uVulkanSpeed = 8.52,
        .rating = ModelRating::Situational,
        .bestFor = QStringLiteral("Clear read-aloud speech, if you can wait"),
        .pros = {QStringLiteral("Fewest mistakes on clear speech")},
        .cons = {QStringLiteral("2.4 GB download"),
                 QStringLiteral("Text appears after you stop"),
                 QStringLiteral("More mistakes than Parakeet on everyday speech")},
    },
    {
        // Q5_K_M: Q4_K_M loses half a point of WER and would be no better than
        // Cohere. Clear-speech WER is the Q5_K_M figure; FLEURS was measured
        // at Q8_0 only. Speed is the Q8_0 run, the nearest quant measured;
        // transcribe.cpp did not run it on the Ryzen 4750U.
        .id = QStringLiteral("voxtral-small"),
        .name = QStringLiteral("Voxtral Small 24B"),
        .huggingFaceRepo = QStringLiteral("handy-computer/Voxtral-Small-24B-2507-gguf"),
        .revision = QStringLiteral("4ab5a0708a16619b2b5b98f14b20333e0de17943"),
        .fileName = QStringLiteral("Voxtral-Small-24B-2507-Q5_K_M.gguf"),
        .sha256 = QStringLiteral("a53f73a5f63b7663fe155977616a636acac567dac898f6ef82ca519017e8b6e7"),
        .sizeBytes = 17138659808,
        .librispeechCleanWer = 1.60,
        .fleursEnglishWer = 3.55,
        .streams = false,
        .licence = QStringLiteral("Apache 2.0"),
        .m4MaxMetalSpeed = 2.41,
        .ryzen4750uVulkanSpeed = 0,
        .rating = ModelRating::NotRecommended,
        .bestFor = QStringLiteral("Nobody: Qwen3-ASR does as well at an eighth of the size"),
        .pros = {QStringLiteral("Few mistakes on everyday speech")},
        .cons = {QStringLiteral("17 GB download"),
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

QString modelFitLabel(ModelFit fit)
{
    switch (fit) {
    case ModelFit::Fits:
        return QStringLiteral("Fits");
    case ModelFit::Tight:
        return QStringLiteral("Tight fit");
    case ModelFit::TooLarge:
        break;
    }
    return QStringLiteral("Too large");
}

QString modelRatingLabel(ModelRating rating)
{
    switch (rating) {
    case ModelRating::Recommended:
        return QStringLiteral("Recommended");
    case ModelRating::Good:
        return QStringLiteral("Good");
    case ModelRating::Situational:
        return QStringLiteral("Situational");
    case ModelRating::NotRecommended:
        break;
    }
    return QStringLiteral("Not recommended");
}

QString downloadSizeText(qint64 bytes)
{
    constexpr double megabyte = 1e6;
    constexpr double gigabyte = 1e9;
    if (bytes < qint64(gigabyte)) {
        return QStringLiteral("%1 MB").arg(qRound(double(bytes) / megabyte));
    }
    return QStringLiteral("%1 GB").arg(double(bytes) / gigabyte, 0, 'f', 1);
}

QString speechSecondsText(double seconds)
{
    return QStringLiteral("%1 s").arg(std::max(seconds, 0.1), 0, 'f', 1);
}

QString wordErrorRateSources()
{
    return QStringLiteral("Clear speech: LibriSpeech test-clean. Everyday speech: FLEURS English.");
}

QString localModelText(LocalModelText text)
{
    switch (text) {
    case LocalModelText::UseModel:
        return QStringLiteral("Use this model");
    case LocalModelText::TestSpeed:
        return QStringLiteral("Test speed");
    case LocalModelText::InUse:
        return QStringLiteral("In use");
    case LocalModelText::Suggested:
        return QStringLiteral("Suggested for this computer");
    case LocalModelText::YourChoice:
        return QStringLiteral("Your choice");
    case LocalModelText::TooLarge:
        return QStringLiteral("Too large for this computer");
    case LocalModelText::HideOtherModels:
        return QStringLiteral("Hide other models");
    case LocalModelText::CompareNote:
        return QStringLiteral("Word errors: clear read speech / everyday speech. Times are estimates until a "
                              "model is downloaded and tested here.");
    }
    return {};
}

QString textShowsValue(bool streams)
{
    return streams ? QStringLiteral("As you speak") : QStringLiteral("After you stop");
}

QString downloadCaption(qint64 bytes)
{
    return QStringLiteral("Download %1").arg(downloadSizeText(bytes));
}

QString compareModelsCaption(int otherModels)
{
    return QStringLiteral("Compare %1 other models").arg(otherModels);
}

QStringList compareTableHeaders()
{
    return {QStringLiteral("Model"), QStringLiteral("Download"), QStringLiteral("Word errors"),
            QStringLiteral("10 s of speech"), QStringLiteral("Text shows"), QStringLiteral("Memory")};
}

QString localModelSpeedLine(const LocalModel &model,
                            const HardwareProfile &hardware,
                            std::optional<double> measuredSeconds)
{
    if (measuredSeconds) {
        return QStringLiteral("%1 per 10 s (measured)").arg(speechSecondsText(*measuredSeconds));
    }
    const std::optional<SpeedEstimate> estimate = estimatedSpeed(model, hardware);
    if (!estimate) {
        return QStringLiteral("Not measured on a computer like this one yet");
    }
    return QStringLiteral("About %1 per 10 s (%2)")
        .arg(speechSecondsText(estimate->secondsFor10sSpeech),
             estimate->measuredOnThisChip ? QStringLiteral("measured on the same chip")
                                          : QStringLiteral("estimated"));
}

} // namespace speecher
