#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/LearnedCorrection.h"
#include "platform/CorrectionDiff.h"
#include "platform/GlobalShortcutBinder.h"
#include "ui/AppWindow.h"

#include <QList>
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
    void correctionTrackerSettlesSamplesWithoutRealTimeWaits()
    {
        CorrectionTracker tracker;
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("I use cute every day");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");

        QList<CorrectionEvidence> observed;
        tracker.begin(window, [&observed](const QString &original,
                                          const QString &corrected,
                                          const QString &,
                                          double confidence) {
            observed.append({original, corrected, confidence});
        });
        tracker.sample(QStringLiteral("before text I use cute every day after text"));
        tracker.sample(QStringLiteral("before text I use Qt every day after text"));
        QCOMPARE(observed.size(), 0);
        tracker.sample(QStringLiteral("before text I use Qt every day after text"));

        QCOMPARE(observed.size(), 1);
        QCOMPARE(observed.first().original, QStringLiteral("cute"));
        QCOMPARE(observed.first().corrected, QStringLiteral("Qt"));
        QVERIFY(!tracker.active());
    }

    void correctionTrackerCancelsUnsettledOrUnreadableSamples()
    {
        CorrectionTracker tracker;
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("cute");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");
        int observations = 0;
        tracker.begin(window, [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        });
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.cancel();
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        tracker.begin(window, [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        });
        tracker.sample(QStringLiteral(
            "before text Qt after text before text duplicate after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        window.target.selectionStart = 0;
        window.target.selectionEnd = 4;
        window.target.selectedText = QStringLiteral("cute");
        tracker.begin(window, [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        });
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);
    }

    void correctionTrackerIgnoresEditsThatAreNotCorrections()
    {
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("cute");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");
        int observations = 0;
        const auto observed = [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        };

        CorrectionTracker untouched;
        untouched.begin(window, observed);
        untouched.sample(QStringLiteral("before text cute after text"));
        untouched.sample(QStringLiteral("before text cute after text"));
        QCOMPARE(observations, 0);
        QVERIFY(untouched.active());

        CorrectionTracker punctuated;
        punctuated.begin(window, observed);
        punctuated.sample(QStringLiteral("before text cute! after text"));
        punctuated.sample(QStringLiteral("before text cute! after text"));
        QCOMPARE(observations, 0);

        CorrectionTracker rewritten;
        rewritten.begin(window, observed);
        rewritten.sample(QStringLiteral("before text an entirely different phrase after text"));
        rewritten.sample(QStringLiteral("before text an entirely different phrase after text"));
        QCOMPARE(observations, 0);
    }

    void correctionTrackerDisablePreventsAndCancelsObservation()
    {
        CorrectionTracker tracker;
        CorrectionWindow window;
        window.target.applicationId = QStringLiteral("org.kde.kate");
        window.original = QStringLiteral("cute");
        window.prefix = QStringLiteral("before text ");
        window.suffix = QStringLiteral(" after text");
        int observations = 0;
        const auto observed = [&observations](const QString &, const QString &,
                                              const QString &, double) {
            ++observations;
        };

        tracker.setEnabled(false);
        tracker.begin(window, observed);
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        tracker.setEnabled(true);
        tracker.begin(window, observed);
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.setEnabled(false);
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 0);

        tracker.setEnabled(true);
        tracker.begin(window, observed);
        tracker.sample(QStringLiteral("before text Qt after text"));
        tracker.sample(QStringLiteral("before text Qt after text"));
        QCOMPARE(observations, 1);
    }

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
