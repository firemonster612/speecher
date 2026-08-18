#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "platform/GlobalShortcutBinder.h"
#include "ui/AppWindow.h"

#include <QSignalSpy>
#include <QTest>

#include <memory>
#include <utility>

using namespace speecher;

namespace {

class FakeGlobalShortcutBinder final : public GlobalShortcutBinder {
public:
    using GlobalShortcutBinder::GlobalShortcutBinder;

    bool supported() const override
    {
        return true;
    }

    QString unsupportedReason() const override
    {
        return {};
    }

    void bind() override
    {
        bindCount += 1;
    }

    QKeySequence shortcut() const override
    {
        return m_shortcut;
    }

    bool setShortcut(const QKeySequence &shortcut, QString *error) override
    {
        if (shortcut.isEmpty()) {
            if (error) {
                *error = QStringLiteral("fake binder rejects an empty sequence");
            }
            return false;
        }
        m_shortcut = shortcut;
        return true;
    }

    int bindCount = 0;

private:
    QKeySequence m_shortcut;
};

// Answers for itself everything the seam added, and delegates the ports it does
// not care about to the composition the platform already provides.
class FakePlatformComposition final : public PlatformComposition {
public:
    explicit FakePlatformComposition(std::shared_ptr<const PlatformComposition> delegate)
        : m_delegate(std::move(delegate))
    {
    }

    QString id() const override
    {
        return QStringLiteral("fake");
    }

    QString outputSummary() const override
    {
        return QStringLiteral("Fake: nothing is delivered");
    }

    QString primaryOutputStatus() const override
    {
        return QStringLiteral("Fake output ready");
    }

    QString ipcListenName() const override
    {
        return m_delegate->ipcListenName();
    }

    QStringList ipcConnectCandidates() const override
    {
        return m_delegate->ipcConnectCandidates();
    }

    QString detachedExecutablePath() const override
    {
        return m_delegate->detachedExecutablePath();
    }

    QList<AudioInputDeviceInfo> availableAudioInputDevices() const override
    {
        return {{QStringLiteral("fake-device"), QStringLiteral("Fake microphone")}};
    }

    AudioInput *createAudioInput(SettingsStore *settings, QObject *parent) const override
    {
        return m_delegate->createAudioInput(settings, parent);
    }

    MediaController *createMediaController(QObject *parent) const override
    {
        return m_delegate->createMediaController(parent);
    }

    TargetProvider *createTargetProvider(QObject *parent) const override
    {
        return m_delegate->createTargetProvider(parent);
    }

    ScreenshotContextProvider *createScreenshotContextProvider(QObject *parent) const override
    {
        return m_delegate->createScreenshotContextProvider(parent);
    }

    TextDeliveryAdapter *createTextDelivery(TargetProvider *targetProvider, QObject *parent) const override
    {
        return m_delegate->createTextDelivery(targetProvider, parent);
    }

    PopupPositioner *createPopupPositioner(QObject *parent) const override
    {
        return m_delegate->createPopupPositioner(parent);
    }

    GlobalShortcutBinder *createGlobalShortcutBinder(QObject *parent) const override
    {
        binder = new FakeGlobalShortcutBinder(parent);
        return binder;
    }

    AccessibilityState accessibilityState() const override
    {
        return {true, true, false};
    }

    bool requestAccessibility(QString *) const override
    {
        return true;
    }

    bool enableAccessibilityPermanently(QString *) const override
    {
        return true;
    }

    mutable FakeGlobalShortcutBinder *binder = nullptr;

private:
    std::shared_ptr<const PlatformComposition> m_delegate;
};

} // namespace

class PlatformCompositionTests : public QObject {
    Q_OBJECT

private slots:
    void controllerAnswersFromTheInjectedComposition()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);

        QCOMPARE(controller.platform(), platform.get());
        QCOMPARE(controller.outputSummary(), QStringLiteral("Fake: nothing is delivered"));
        QCOMPARE(controller.primaryOutputStatus(), QStringLiteral("Fake output ready"));
    }

    void shortcutApiDelegatesToTheCompositionsBinder()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);

        QVERIFY(controller.globalShortcutsSupported());
        QVERIFY(controller.globalShortcut().isEmpty());

        QString error;
        QVERIFY(!controller.setGlobalShortcut(QKeySequence(), &error));
        QCOMPARE(error, QStringLiteral("fake binder rejects an empty sequence"));

        const QKeySequence chosen(Qt::META | Qt::ALT | Qt::Key_D);
        QVERIFY(controller.setGlobalShortcut(chosen));
        QCOMPARE(controller.globalShortcut(), chosen);
    }

    void deferredStartupBindsTheShortcutAndPublishesAccessibility()
    {
        const auto platform = std::make_shared<FakePlatformComposition>(platformComposition());
        ApplicationController controller(true, platform);
        QSignalSpy accessibilityChanged(&controller,
                                        &ApplicationController::accessibilityStateChanged);
        QVERIFY(platform->binder);
        QCOMPARE(platform->binder->bindCount, 0);

        AppWindow window(&controller);
        window.show();
        QTRY_COMPARE_WITH_TIMEOUT(accessibilityChanged.count(), 1, 250);

        QCOMPARE(platform->binder->bindCount, 1);
        QVERIFY(controller.accessibilitySupported());
        QVERIFY(controller.accessibilityEnabled());
        QVERIFY(!controller.accessibilityPersistent());
    }
};

int runPlatformCompositionTests(int argc, char **argv)
{
    PlatformCompositionTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_platform_composition.moc"
