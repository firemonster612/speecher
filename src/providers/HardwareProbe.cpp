#include "providers/HardwareProbe.h"

#include <QFile>
#include <QThread>

#include <optional>

namespace speecher {
namespace {

using Device = LocalSpeechEngine::Device;

#ifdef Q_OS_LINUX
// ggml's CPU device reports free memory as equal to total everywhere but
// Windows, so Linux reads what the kernel says it can hand out.
std::optional<quint64> linuxAvailableRam()
{
    QFile meminfo(QStringLiteral("/proc/meminfo"));
    if (!meminfo.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    for (const QByteArray &line : meminfo.readAll().split('\n')) {
        if (line.startsWith("MemAvailable:")) {
            const QList<QByteArray> fields = line.simplified().split(' ');
            return fields.value(1).toULongLong() * 1024;
        }
    }
    return std::nullopt;
}
#endif

} // namespace

HardwareSummary probeHardware()
{
    HardwareSummary summary;
    summary.cpuThreads = QThread::idealThreadCount();
    const Device *dedicated = nullptr;
    bool integrated = false;
    const QList<Device> devices = LocalSpeechEngine::devices();
    for (const Device &device : devices) {
        switch (device.type) {
        case Device::Type::Cpu:
            // Windows pads the processor brand string with spaces.
            summary.profile.chipName = device.description.simplified();
            summary.profile.systemRamBytes = device.memoryTotalBytes;
            summary.profile.availableRamBytes = device.memoryFreeBytes;
            break;
        case Device::Type::Gpu:
            summary.gpus.append(device);
            if (!dedicated || device.memoryTotalBytes > dedicated->memoryTotalBytes) {
                dedicated = &device;
            }
            break;
        case Device::Type::IntegratedGpu:
            summary.gpus.append(device);
            integrated = true;
            break;
        case Device::Type::Accelerator:
            break;
        }
    }
#ifdef Q_OS_LINUX
    summary.profile.availableRamBytes =
        linuxAvailableRam().value_or(summary.profile.availableRamBytes);
#endif
    if (dedicated) {
        // Metal is only built for Apple Silicon, whose GPU shares system RAM.
        summary.profile.accelerator = dedicated->kind == QStringLiteral("metal")
            ? HardwareProfile::Accelerator::AppleSilicon
            : HardwareProfile::Accelerator::DedicatedGpu;
        summary.profile.gpuMemoryBytes = dedicated->memoryTotalBytes;
    } else if (integrated) {
        summary.profile.accelerator = HardwareProfile::Accelerator::IntegratedGpu;
    }
    return summary;
}

} // namespace speecher
