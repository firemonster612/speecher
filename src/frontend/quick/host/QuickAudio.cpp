#include "frontend/quick/host/QuickAudio.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/SettingsStore.h"

#include <QMediaDevices>

#include <utility>

namespace speecher {

namespace {

QuickAudio *g_instance = nullptr;

} // namespace

QuickAudio::QuickAudio(ApplicationController *controller,
                       QObject *parent,
                       DeviceLister listDevices,
                       MicrophoneTest::InputFactory createInput)
    : QObject(parent)
    , m_controller(controller)
    , m_listDevices(std::move(listDevices))
    , m_test(*controller, nullptr, std::move(createInput))
{
    g_instance = this;
    if (!m_listDevices) {
        m_listDevices = [controller] { return controller->platform()->availableAudioInputDevices(); };
        connect(new QMediaDevices(this), &QMediaDevices::audioInputsChanged, this, &QuickAudio::refreshDevices);
    }
    connect(&m_test, &MicrophoneTest::changed, this, [this] {
        switch (m_test.state()) {
        case MicrophoneTestState::Starting:
            m_testError.clear();
            m_testRan = false;
            break;
        case MicrophoneTestState::Running:
            m_testRan = true;
            break;
        case MicrophoneTestState::Stopped:
            // Soon: a failure's reason arrives just after the stop.
            keepTestRunningSoon();
            break;
        }
        if (m_test.state() != MicrophoneTestState::Running) {
            setLevel(0);
        }
        emit testChanged();
    });
    connect(&m_test, &MicrophoneTest::levelChanged, this, &QuickAudio::setLevel);
    connect(&m_test, &MicrophoneTest::failed, this, [this](const QString &deviceId, const QString &message) {
        m_testError = message;
        // A device lost while running may be back, or another may record in
        // its place, so it is tried once more. One that would not open fails
        // the same way each time, and one that fails twice may keep failing.
        // A device no longer chosen holds nothing back: the choice moved on.
        if (deviceId != m_controller->settings()->audioInputDeviceId()) {
            m_failure.reset();
            emit testChanged();
            return;
        }
        const bool failedBefore = m_failure && m_failure->deviceId == deviceId;
        m_failure = DeviceFailure{deviceId, !m_testRan || failedBefore};
        emit testChanged();
    });
    // Another device saved, or microphones coming and going, may open now.
    connect(controller->settings(), &SettingsStore::audioCaptureSettingsChanged, this,
            [this](const AudioCaptureSettings &capture) {
        if (m_failure && m_failure->deviceId != capture.deviceId) {
            m_failure.reset();
        }
        keepTestRunningSoon();
    });
    connect(this, &QuickAudio::devicesChanged, this, [this] {
        m_failure.reset();
        keepTestRunningSoon();
    });
    refreshDevices();
}

QuickAudio::~QuickAudio()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QuickAudio *QuickAudio::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(g_instance, "QuickAudio::create", "the host makes Audio before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QString QuickAudio::testState() const
{
    switch (m_test.state()) {
    case MicrophoneTestState::Starting:
        return QStringLiteral("starting");
    case MicrophoneTestState::Running:
        return QStringLiteral("running");
    case MicrophoneTestState::Stopped:
        break;
    }
    return QStringLiteral("stopped");
}

void QuickAudio::refreshDevices()
{
    QVariantList devices;
    for (const AudioInputDeviceInfo &device : m_listDevices()) {
        devices.append(QVariantMap{{QStringLiteral("id"), device.id},
                                   {QStringLiteral("label"), device.label},
                                   {QStringLiteral("isDefault"), device.isDefault}});
    }
    if (devices == m_devices) {
        return;
    }
    m_devices = devices;
    emit devicesChanged();
}

void QuickAudio::holdTest(QObject *holder)
{
    if (!holder) {
        return;
    }
    if (!m_holders.contains(holder)) {
        m_holders.append(holder);
        connect(holder, &QObject::destroyed, this, [this, holder] { releaseTest(holder); });
        emit testChanged();
    }
    m_failure.reset();
    keepTestRunning();
}

void QuickAudio::releaseTest(QObject *holder)
{
    if (!m_holders.removeOne(holder)) {
        return;
    }
    disconnect(holder, &QObject::destroyed, this, nullptr);
    if (m_holders.isEmpty()) {
        m_test.stop();
    }
    emit testChanged();
}

void QuickAudio::keepTestRunning()
{
    const QString deviceId = m_controller->settings()->audioInputDeviceId();
    if (m_holders.isEmpty() || (m_failure && m_failure->waits && m_failure->deviceId == deviceId)) {
        return;
    }
    // Does nothing unless the test is stopped and free to start.
    m_test.start(deviceId);
}

void QuickAudio::keepTestRunningSoon()
{
    QMetaObject::invokeMethod(this, &QuickAudio::keepTestRunning, Qt::QueuedConnection);
}

void QuickAudio::setLevel(qreal level)
{
    if (m_level == level) {
        return;
    }
    m_level = level;
    emit levelChanged();
}

} // namespace speecher
