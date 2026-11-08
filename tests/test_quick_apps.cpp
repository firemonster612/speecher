// The Apps singleton's own behavior: Switch to it's capture of another app
// (AppCapture) and the edits the Apps page makes.

#include "common/test_doubles.h"
#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/AppCapture.h"
#include "frontend/quick/host/QuickApps.h"

#include <QSignalSpy>

using namespace speecher;

namespace {

// Ticks short enough for a test to wait out, and a wait of three ticks.
constexpr AppCaptureTiming quick{.tickMs = 5, .waitTicks = 3, .settleMs = 5};

// An AppCapture over a fake provider, with a switch for whether Speecher is
// in front and one for a dictation under way, which keeps the countdown at
// each change and the Target it captured.
struct Capture {
    explicit Capture(AppCaptureTiming timing = quick)
        : capture(&provider, [this] { return busy; }, [this] { return inFront; }, timing)
    {
        QObject::connect(&capture, &AppCapture::changed, [this] { countdowns.append(capture.countdown()); });
        QObject::connect(&capture, &AppCapture::captured, [this](const Target &target) { captured.append(target); });
    }

    test::FakeTargetProvider provider;
    bool inFront = true;
    bool busy = false;
    AppCapture capture;
    QList<int> countdowns;
    QList<Target> captured;
};

} // namespace

class QuickAppsTests : public QObject {
    Q_OBJECT

private slots:
    // The countdown goes down a second a tick, then it waits while Speecher
    // stays in front, and captures the app that comes to the front instead.
    void countsDownThenCapturesTheAppThatComesToTheFront()
    {
        // A wait long enough not to run out while the test looks.
        Capture fixture({.tickMs = 5, .waitTicks = 1000, .settleMs = 5});
        fixture.provider.target.applicationId = QStringLiteral("org.kde.kate");
        QSignalSpy failed(&fixture.capture, &AppCapture::failed);

        fixture.capture.start(2);
        QVERIFY(fixture.capture.waiting());
        QTRY_COMPARE(fixture.countdowns, (QList<int>{2, 1, 0}));
        QTRY_VERIFY(fixture.capture.countdown() < -2);
        QVERIFY(fixture.capture.waiting());
        QCOMPARE(fixture.provider.captureCalls, 0);

        fixture.inFront = false;
        QTRY_COMPARE(fixture.captured.size(), 1);
        QCOMPARE(fixture.captured.first().applicationId, QStringLiteral("org.kde.kate"));
        QVERIFY(!fixture.capture.waiting());
        QCOMPARE(failed.count(), 0);
    }

    // With Speecher in front past the wait, it gives up without asking the
    // provider.
    void givesUpWhenNoOtherAppComesToTheFront()
    {
        Capture fixture;
        QSignalSpy failed(&fixture.capture, &AppCapture::failed);
        fixture.capture.start(0);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(!fixture.capture.waiting());
        QCOMPARE(fixture.provider.captureCalls, 0);
    }

    // Capturing would drop what a dictation under way captured.
    void failsWhileADictationIsUnderWay()
    {
        Capture fixture;
        fixture.inFront = false;
        fixture.busy = true;
        QSignalSpy failed(&fixture.capture, &AppCapture::failed);
        fixture.capture.start(0);
        QTRY_COMPARE(failed.count(), 1);
        QCOMPARE(fixture.provider.captureCalls, 0);
    }

    // A capture the provider answers after a cancel answers nothing.
    void aCancelledCaptureAnswersNothing()
    {
        Capture fixture;
        fixture.inFront = false;
        fixture.provider.holdsCapture = true;
        QSignalSpy failed(&fixture.capture, &AppCapture::failed);
        fixture.capture.start(0);
        QTRY_COMPARE(fixture.provider.captureCalls, 1);
        fixture.capture.cancel();
        QVERIFY(!fixture.capture.waiting());
        fixture.provider.answerCapture();
        QVERIFY(fixture.captured.isEmpty());
        QCOMPARE(failed.count(), 0);
    }

    // A type the person sets on an App migrated from a category-only rule
    // takes that type's profile, as for any App, rather than the catalog's.
    void choosingATypeEndsTheCatalogsProfile()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            AppEntry kate;
            kate.id = QStringLiteral("app:kate");
            kate.name = QStringLiteral("Kate");
            kate.matches = {{AppMatchKind::Id, QStringLiteral("org.kde.kate")}};
            kate.type = AppCategory::General;
            kate.catalogPicksProfile = true;
            draft.apps.apps = {kate};
            return true;
        });
        QuickApps apps(&controller);
        QCOMPARE(apps.update(QStringLiteral("app:kate"), {{QStringLiteral("type"), QStringLiteral("email")}}), QString());
        QTRY_VERIFY(!controller.settings()->snapshot().apps.apps.value(0).catalogPicksProfile);
    }
};

int runQuickAppsTests(int argc, char **argv)
{
    QuickAppsTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_apps.moc"
