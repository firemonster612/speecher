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
        if (m_test.state() == MicrophoneTestState::Starting) {
            m_testError.clear();
        }
        if (m_test.state() != MicrophoneTestState::Running) {
            setLevel(0);
        }
        emit testChanged();
    });
    connect(&m_test, &MicrophoneTest::levelChanged, this, &QuickAudio::setLevel);
    connect(&m_test, &MicrophoneTest::failed, this, [this](const QString &message) {
        m_testError = message;
        emit testChanged();
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

void QuickAudio::toggleTest()
{
    m_test.toggle(m_controller->settings()->audioInputDeviceId());
}

void QuickAudio::stopTest()
{
    m_test.stop();
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
