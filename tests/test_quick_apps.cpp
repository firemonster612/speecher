// The Apps singleton's own behavior: Switch to it's capture of another app
// (AppCapture) and the edits the Apps page makes.

#include "common/test_doubles.h"
#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SeenApps.h"
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

    void customTypeRecordsAndEditsUseTheirIds()
    {
        ApplicationController controller(true);
        QuickApps apps(&controller);
        QCOMPARE(apps.addType(QStringLiteral(" ")).value(QStringLiteral("refusal")).toString(), QStringLiteral("blank"));
        const QVariantMap added = apps.addType(QStringLiteral("Projects"));
        const QString id = added.value(QStringLiteral("id")).toString();
        QVERIFY(id.startsWith(kCustomIdPrefix));
        QCOMPARE(added.value(QStringLiteral("refusal")).toString(), QString());
        QCOMPARE(apps.types().at(6).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Projects"));
        QVERIFY(apps.types().at(6).toMap().value(QStringLiteral("custom")).toBool());
        QVERIFY(!apps.types().first().toMap().value(QStringLiteral("custom")).toBool());
        const QString appId = apps.add({{QStringLiteral("name"), QStringLiteral("Notes")},
                                       {QStringLiteral("ids"), QStringList{QStringLiteral("notes")}}})
                                  .value(QStringLiteral("id")).toString();
        QCOMPARE(apps.update(appId, {{QStringLiteral("type"), id}}), QString());
        QCOMPARE(apps.updateType(id, {{QStringLiteral("name"), QStringLiteral("My projects")},
                                     {QStringLiteral("profile"), WritingProfile::Personal}}), QString());
        QCOMPARE(apps.entries().first().toMap().value(QStringLiteral("type")).toString(), id);
        QCOMPARE(apps.entries().first().toMap().value(QStringLiteral("effective")).toMap().value(QStringLiteral("type")).toString(), id);
        QCOMPARE(apps.entries().first().toMap().value(QStringLiteral("inherited")).toMap().value(QStringLiteral("type")).toString(), QStringLiteral("general"));
        const int undo = apps.removeType(id);
        QVERIFY(undo >= 0);
        QCOMPARE(apps.entries().first().toMap().value(QStringLiteral("type")).toString(), QString());
        apps.undo(undo);
        QCOMPARE(apps.entries().first().toMap().value(QStringLiteral("type")).toString(), id);
        QCOMPARE(apps.types().at(6).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("My projects"));
        QCOMPARE(apps.removeType(QStringLiteral("general")), -1);
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
            kate.type = appCategoryName(AppCategory::General);
            kate.catalogPicksProfile = true;
            draft.apps.apps = {kate};
            return true;
        });
        QuickApps apps(&controller);
        QCOMPARE(apps.update(QStringLiteral("app:kate"), {{QStringLiteral("type"), QStringLiteral("email")}}), QString());
        QTRY_VERIFY(!controller.settings()->snapshot().apps.apps.value(0).catalogPicksProfile);
    }

    // An App picked by its id is sorted as the app it was picked as, by the
    // name that app goes by, and not by the App's own name, which the person
    // may change.
    void anAppPickedByIdIsSortedByItsAppsReportedName()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            AppEntry wave;
            wave.id = QStringLiteral("app:wave");
            wave.name = QStringLiteral("Wave Browser");
            wave.matches = {{AppMatchKind::Id, QStringLiteral("org.example.Wave")}};
            draft.apps.apps = {wave};
            return true;
        });
        DictationRecord seen;
        seen.appName = QStringLiteral("Wave Browser");
        seen.applicationId = QStringLiteral("org.example.Wave");
        controller.seenApps()->record(seen);
        QuickApps apps(&controller);
        const auto effectiveType = [&apps] {
            return apps.entries().value(0).toMap().value(QStringLiteral("effective")).toMap().value(QStringLiteral("type"));
        };
        QTRY_COMPARE(effectiveType(), QVariant(QStringLiteral("browser")));
        QCOMPARE(apps.update(QStringLiteral("app:wave"), {{QStringLiteral("name"), QStringLiteral("Work mail")}}), QString());
        QTRY_COMPARE(apps.entries().value(0).toMap().value(QStringLiteral("name")), QVariant(QStringLiteral("Work mail")));
        QCOMPARE(effectiveType(), QVariant(QStringLiteral("browser")));
    }
};

int runQuickAppsTests(int argc, char **argv)
{
    QuickAppsTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_apps.moc"
