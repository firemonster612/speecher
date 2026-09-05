#include "app/WindowsComposition.h"

#include "app/CompositionSockets.h"
#include "core/SettingsStore.h"
#include "output/TextDelivery.h"
#include "platform/FallbackPopupPositioner.h"
#include "platform/GlobalShortcutBinder.h"
#include "platform/audio/QtAudioInput.h"

#include <QCoreApplication>

namespace speecher {
namespace {

constexpr auto unsupportedMessage = "This feature is unsupported in the Windows hosting spike";

class NullMediaController final : public MediaController {
public:
    using MediaController::MediaController;

    void pausePlaying() override {}
    void resumePaused() override {}
};

class NullTargetProvider final : public TargetProvider {
public:
    using TargetProvider::TargetProvider;

    Target capture(const QList<AppRecognitionRule> &) override { return {}; }
};

class NullScreenshotContextProvider final : public ScreenshotContextProvider {
public:
    using ScreenshotContextProvider::ScreenshotContextProvider;

    void capture() override { emit failed(QString::fromLatin1(unsupportedMessage)); }
    void cancel() override {}
};

class UnsupportedGlobalShortcutBinder final : public GlobalShortcutBinder {
public:
    using GlobalShortcutBinder::GlobalShortcutBinder;

    bool supported() const override { return false; }
    QString unsupportedReason() const override { return QString::fromLatin1(unsupportedMessage); }
    void bind() override {}
    QKeySequence shortcut() const override { return defaultShortcut(); }
    bool setShortcut(const QKeySequence &, QString *error) override
    {
        if (error) {
            *error = unsupportedReason();
        }
        return false;
    }
};

} // namespace

QString WindowsComposition::outputSummary() const
{
    return QStringLiteral("Automatic: copies to the clipboard");
}

QString WindowsComposition::ipcListenName() const
{
    return appSocketName();
}

QStringList WindowsComposition::ipcConnectCandidates() const
{
    return {appSocketName(), executablePathSocketName()};
}

QString WindowsComposition::detachedExecutablePath() const
{
    return QCoreApplication::applicationFilePath();
}

QList<AudioInputDeviceInfo> WindowsComposition::availableAudioInputDevices() const
{
    return QtAudioInput::availableInputDevices();
}

AudioInput *WindowsComposition::createAudioInput(SettingsStore *settings, QObject *parent) const
{
    auto *input = new QtAudioInput(settings->audioCaptureSettings(), parent);
    QObject::connect(settings,
                     &SettingsStore::audioCaptureSettingsChanged,
                     input,
                     &QtAudioInput::applySettings);
    return input;
}

MediaController *WindowsComposition::createMediaController(QObject *parent) const
{
    return new NullMediaController(parent);
}

TargetProvider *WindowsComposition::createTargetProvider(QObject *parent) const
{
    return new NullTargetProvider(parent);
}

ScreenshotContextProvider *WindowsComposition::createScreenshotContextProvider(QObject *parent) const
{
    return new NullScreenshotContextProvider(parent);
}

TextDeliveryAdapter *WindowsComposition::createTextDelivery(TargetProvider *targetProvider, QObject *parent) const
{
    return new TextDelivery(targetProvider, parent);
}

PopupPositioner *WindowsComposition::createPopupPositioner(QObject *parent) const
{
    return new FallbackPopupPositioner(parent);
}

GlobalShortcutBinder *WindowsComposition::createGlobalShortcutBinder(QObject *parent) const
{
    return new UnsupportedGlobalShortcutBinder(parent);
}

AccessibilityState WindowsComposition::accessibilityState() const
{
    return {true, true, true};
}

bool WindowsComposition::requestAccessibility(QString *error) const
{
    Q_UNUSED(error);
    return true;
}

bool WindowsComposition::enableAccessibilityPermanently(QString *error) const
{
    Q_UNUSED(error);
    return true;
}

std::shared_ptr<const WindowsComposition> windowsComposition()
{
    return std::make_shared<WindowsComposition>();
}

} // namespace speecher
