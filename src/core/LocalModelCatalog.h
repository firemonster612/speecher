#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace speecher {

// How a model compares with the rest of the catalog, shown as a badge next to
// its name. Separate from the Hardware Tier's suggestion for this computer.
enum class ModelRating {
    Recommended,
    Good,
    // Worth it only for the case bestFor names.
    Situational,
    // Kept for people who already downloaded it; another model beats it.
    NotRecommended,
};

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
    ModelRating rating = ModelRating::Good;
    // Who should pick it, in one short line: "Fewest mistakes if you can wait".
    QString bestFor;
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

// The words every front end shows for these facts, so the setup assistants
// and Local models pages agree.
QString modelFitLabel(ModelFit fit);
// The badge's words: "Recommended", "Good", "Situational", "Not recommended".
QString modelRatingLabel(ModelRating rating);
// "731 MB", "2.4 GB": decimal units, as download sizes are quoted.
QString downloadSizeText(qint64 bytes);
// "0.3 s", never below 0.1 s.
QString speechSecondsText(double seconds);
// Where the two word error rates come from, for a tooltip on them.
QString wordErrorRateSources();
// The fact names beside a model in the Local models page's detail.
struct LocalModelFactLabels {
    QString bestFor = QStringLiteral("Best for");
    QString download = QStringLiteral("Download");
    QString speedHere = QStringLiteral("Speed here");
    QString wordErrorRate = QStringLiteral("Word error rate");
    QString textShows = QStringLiteral("Text shows");
    QString language = QStringLiteral("Language");
    QString license = QStringLiteral("License");
};
// The fixed wording the Local models page and the setup assistants' Local
// Model card share.
enum class LocalModelText {
    UseModel,
    TestSpeed,
    InUse,
    Suggested,
    YourChoice,
    TooLarge,
    HideOtherModels,
    CompareNote,
    // Deleting a downloaded model: the button and its confirmation's body.
    DeleteModel,
    DeleteBody,
};
QString localModelText(LocalModelText text);
// The delete confirmation's question, "Delete Whisper Small?".
QString deleteModelQuestion(const QString &modelName);
// "As you speak" for a streaming model, else "After you stop".
QString textShowsValue(bool streams);
// "Download 731 MB".
QString downloadCaption(qint64 bytes);
// "Compare 7 other models".
QString compareModelsCaption(int otherModels);
// The comparison table's columns: Model, Download, Word errors, 10 s of
// speech, Text shows, Memory.
QStringList compareTableHeaders();

// How long 10 s of speech takes here: the Speed Test's measurement when there
// is one, else the catalog's estimate for this kind of machine.
QString localModelSpeedLine(const LocalModel &model,
                            const HardwareProfile &hardware,
                            std::optional<double> measuredSeconds);

} // namespace speecher
