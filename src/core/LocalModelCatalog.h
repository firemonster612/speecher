#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace speecher {

// One Local Model Speecher offers. The numbers come from transcribe.cpp
// v0.2.4's catalog/<variant>.json and the pinned Hugging Face revision; see
// LocalModelCatalog.cpp.
struct LocalModel {
    QString id;
    QString name;
    QString huggingFaceRepo;
    QString revision;
    QString fileName;
    QString sha256;
    qint64 sizeBytes = 0;
    double librispeechCleanWer = 0;
    double fleursEnglishWer = 0;
    bool streams = false;
    QString licence;
    // Multiples of real time on transcribe.cpp's reference machines; 0 where
    // it published none.
    double m4MaxMetalSpeed = 0;
    double ryzen4750uVulkanSpeed = 0;
    QStringList pros;
    QStringList cons;
};

const QList<LocalModel> &localModelCatalog();
const LocalModel *findLocalModel(const QString &id);

// What the Hardware Tier needs to know about a machine. Memory is in bytes.
struct HardwareProfile {
    enum class Accelerator {
        Cpu,
        IntegratedGpu,
        DedicatedGpu,
        AppleSilicon,
    };
    Accelerator accelerator = Accelerator::Cpu;
    // The processor's brand string, e.g. "Apple M4 Max".
    QString chipName;
    quint64 systemRamBytes = 0;
    quint64 availableRamBytes = 0;
    // Dedicated GPU memory, or the Metal working set on Apple Silicon.
    quint64 gpuMemoryBytes = 0;
};

quint64 memoryBudgetBytes(const HardwareProfile &hardware);

enum class ModelFit {
    Fits,
    Tight,
    TooLarge,
};

ModelFit modelFit(const LocalModel &model, const HardwareProfile &hardware);

struct SpeedEstimate {
    double secondsFor10sSpeech = 0;
    // False when the machine is not the one the catalog measured on.
    bool measuredOnThisChip = false;
};

// Before a download: scaled from the nearest reference machine, Apple to the
// M4 Max, anything else to the Ryzen 4750U. Empty when the catalog has no
// figure for that machine.
std::optional<SpeedEstimate> estimatedSpeed(const LocalModel &model, const HardwareProfile &hardware);

// The model the setup assistant and Local models page put first.
const LocalModel &suggestedLocalModel(const HardwareProfile &hardware);

} // namespace speecher
