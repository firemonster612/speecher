#include "common/service_order_rows.h"
#include "common/test_suites.h"
#include "common/test_system_singletons.h"

#include "app/ApplicationController.h"
#include "core/history/HistoryStore.h"
#include "core/SeenApps.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "frontend/quick/host/QuickAudio.h"
#include "frontend/quick/host/QuickHistory.h"
#include "frontend/quick/host/QuickInsights.h"
#include "frontend/quick/host/QuickModels.h"
#include "frontend/quick/host/QuickNavigation.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/QuickDictation.h"
#include "frontend/quick/host/QuickRecording.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJSValue>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>
#include <set>

// The UI's QML modules are linked into the tests by test_quick_settings.cpp,
// which imports their plugins for the whole binary.

using namespace speecher;

namespace {

// Saturday 26 September 2026.
const QDate kToday(2026, 9, 26);

QString dataPath(const QString &name)
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QLatin1Char('/') + name;
}

// What a controller left of History and the apps dictated into.
void removeHistory()
{
    for (const char *name : {"history.sqlite3", "history.sqlite3-wal", "history.sqlite3-shm"}) {
        QFile::remove(dataPath(QString::fromLatin1(name)));
    }
    QDir(dataPath(QStringLiteral("seen-apps.json"))).removeRecursively();
    QFile::remove(dataPath(QStringLiteral("seen-apps.json")));
}

// A delivered dictation as the session reports it while History keeps stats.
HistoryEntry finished(const DictationRecord &record)
{
    HistoryEntry entry;
    entry.stats = record;
    entry.kept = HistoryKeep::Stats;
    return entry;
}

void addStats(ApplicationController &controller, const DictationRecord &record)
{
    controller.history()->add(finished(record));
}

DictationRecord recordOn(const QDate &date, int words, const QString &app = QStringLiteral("Kate"),
                         const QString &profile = WritingProfile::Other)
{
    return {QDateTime(date, QTime(10, 0)), 6000, words, app, profile};
}

QDate dateOf(const QVariant &value)
{
    return value.toDateTime().date();
}

QVariantMap summaryOf(const QuickInsights &insights)
{
    return insights.summary().toMap();
}

// The first item under `root` whose text is `text`.
QQuickItem *findText(QQuickItem *root, const QString &text)
{
    if (root->property("text").toString() == text) {
        return root;
    }
    for (QQuickItem *child : root->childItems()) {
        if (QQuickItem *found = findText(child, text)) {
            return found;
        }
    }
    return nullptr;
}

// The non-empty texts of the figure tooltips under `root`. A tooltip is a
// child object of its item, not an item, so findText doesn't reach it.
QStringList figureTips(QQuickItem *root)
{
    QStringList tips;
    if (auto *tip = root->findChild<QObject *>(QStringLiteral("figureTip"), Qt::FindDirectChildrenOnly)) {
        if (const QString text = tip->property("text").toString(); !text.isEmpty()) {
            tips << text;
        }
    }
    for (QQuickItem *child : root->childItems()) {
        tips << figureTips(child);
    }
    return tips;
}

// A computer with one microphone, so Home has nothing in its way there.
QList<AudioInputDeviceInfo> oneMicrophone()
{
    return {{QStringLiteral("mic"), QStringLiteral("Microphone"), true}};
}

} // namespace

// The UI's Insights, over the controller's insights log, its settings and
// its latest transcript, with today pinned to kToday.
class QuickInsightsTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        qputenv("SPEECHER_INSIGHTS_TODAY", kToday.toString(Qt::ISODate).toUtf8());
    }

    void cleanupTestCase()
    {
        qunsetenv("SPEECHER_INSIGHTS_TODAY");
    }

    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
        removeHistory();
    }

    void cleanup()
    {
        removeHistory();
    }

    void stateFollowsTheLogAndKeepInsights()
    {
        ApplicationController controller(true);
        QuickInsights insights(&controller);
        QCOMPARE(insights.state(), QStringLiteral("empty"));
        QVERIFY(insights.summary().isNull());
        QVERIFY(insights.firstDictation().isNull());

        QSignalSpy changed(&insights, &QuickInsights::changed);
        addStats(controller, recordOn(kToday.addDays(-3), 40));
        QCOMPARE(insights.state(), QStringLiteral("ready"));
        QCOMPARE(insights.kept(), 1);
        QCOMPARE(dateOf(insights.firstDictation()), kToday.addDays(-3));
        QVERIFY(changed.count() >= 1);

        // Turned off, nothing shows, and what was kept stays until cleared.
        AppSettings settings = controller.settings()->snapshot();
        settings.history.keeps = HistoryKeep::Nothing;
        controller.settings()->applySnapshot(settings);
        QCOMPARE(insights.state(), QStringLiteral("off"));
        QVERIFY(!insights.hasData());
        QVERIFY(insights.summary().isNull());
        QCOMPARE(insights.kept(), 1);
    }

    void summaryCarriesCoresNumbersForThePeriod()
    {
        ApplicationController controller(true);
        addStats(controller, recordOn(kToday.addDays(-70), 500, QStringLiteral("Slack"), WritingProfile::Work));
        addStats(controller, recordOn(kToday.addDays(-1), 300, QStringLiteral("Slack"), WritingProfile::Work));
        addStats(controller, recordOn(kToday, 100, QStringLiteral("Kate"), WritingProfile::Email));
        QuickInsights insights(&controller);
        QCOMPARE(insights.period(), QStringLiteral("30d"));
        QCOMPARE(dateOf(insights.today()), kToday);

        QVariantMap summary = summaryOf(insights);
        QCOMPARE(summary.value(QStringLiteral("words")).toLongLong(), 400);
        QCOMPARE(summary.value(QStringLiteral("dictations")).toInt(), 2);
        // The 30 days before held nothing to compare with: null, not 0.
        QVERIFY(summary.value(QStringLiteral("wordsDelta")).isNull());
        const QVariantList profiles = summary.value(QStringLiteral("profiles")).toList();
        QCOMPARE(profiles.size(), 2);
        QCOMPARE(profiles.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Work"));
        QCOMPARE(profiles.at(0).toMap().value(QStringLiteral("percent")).toInt(), 75);
        QCOMPARE(profiles.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Email"));
        const QVariantMap streak = summary.value(QStringLiteral("streak")).toMap();
        QCOMPARE(streak.value(QStringLiteral("current")).toInt(), 2);
        QVERIFY(streak.value(QStringLiteral("dictatedToday")).toBool());
        QCOMPARE(dateOf(summary.value(QStringLiteral("longest")).toMap().value(QStringLiteral("date"))),
                 kToday.addDays(-70));
        QCOMPARE(insights.heatmap().size(), 53 * 7 - 1);
        QCOMPARE(dateOf(insights.heatmap().last().toMap().value(QStringLiteral("date"))), kToday);

        // 900 words in all: 1,000 is next, nothing passed yet.
        const QVariantList milestones = summary.value(QStringLiteral("milestones")).toList();
        QCOMPARE(milestones.size(), 2);
        QCOMPARE(milestones.at(0).toMap().value(QStringLiteral("words")).toInt(), 1000);
        QCOMPARE(milestones.at(0).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("next"));
        QCOMPARE(milestones.at(0).toMap().value(QStringLiteral("progress")).toDouble(), 0.9);
        QCOMPARE(milestones.at(1).toMap().value(QStringLiteral("state")).toString(), QStringLiteral("later"));

        QSignalSpy periodChanged(&insights, &QuickInsights::periodChanged);
        insights.setPeriod(QStringLiteral("all"));
        QCOMPARE(periodChanged.count(), 1);
        QCOMPARE(summaryOf(insights).value(QStringLiteral("words")).toLongLong(), 900);

        QTest::ignoreMessage(QtWarningMsg, "Insights: no period fortnight");
        insights.setPeriod(QStringLiteral("fortnight"));
        QCOMPARE(insights.period(), QStringLiteral("all"));
    }

    void heatLevelsFollowTheMeasure()
    {
        ApplicationController controller(true);
        // Yesterday: one long dictation. Today: three short ones.
        addStats(controller, recordOn(kToday.addDays(-1), 900));
        for (int i = 0; i < 3; ++i) {
            addStats(controller, recordOn(kToday, 10));
        }
        QuickInsights insights(&controller);
        const qsizetype last = insights.heatmap().size() - 1;
        QVariantList levels = insights.heatLevels();
        QCOMPARE(levels.at(last).toInt(), 4);
        QCOMPARE(levels.at(last - 1).toInt(), 1);
        QCOMPARE(levels.at(0).toInt(), 0);

        QSignalSpy levelsChanged(&insights, &QuickInsights::heatLevelsChanged);
        insights.setHeatMeasure(QStringLiteral("words"));
        QCOMPARE(levelsChanged.count(), 1);
        levels = insights.heatLevels();
        QCOMPARE(levels.at(last).toInt(), 1);
        QCOMPARE(levels.at(last - 1).toInt(), 4);
    }

    void clearDeletesTheHistoryOrSaysItCouldNot()
    {
        ApplicationController controller(true);
        DictationRecord seen = recordOn(kToday, 10);
        seen.applicationId = QStringLiteral("org.kde.kate");
        // The session's entry, which it makes only while History keeps
        // stats, goes to History and the apps dictated into.
        emit controller.session()->dictationFinished(finished(seen), false);
        QCOMPARE(controller.seenApps()->apps().size(), 1);
        QuickInsights insights(&controller);
        QCOMPARE(insights.kept(), 1);

        // A non-empty directory where the apps' file was: it cannot be
        // deleted, and History stays as it was.
        QVERIFY(QFile::remove(dataPath(QStringLiteral("seen-apps.json"))));
        QVERIFY(QDir().mkpath(dataPath(QStringLiteral("seen-apps.json/keep"))));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("could not be deleted")));
        QVERIFY(!insights.clear());
        QCOMPARE(insights.kept(), 1);
        QCOMPARE(insights.state(), QStringLiteral("ready"));

        QVERIFY(QDir(dataPath(QStringLiteral("seen-apps.json"))).removeRecursively());
        QVERIFY(insights.clear());
        QCOMPARE(insights.kept(), 0);
        QVERIFY(controller.seenApps()->apps().isEmpty());
        QCOMPARE(insights.state(), QStringLiteral("empty"));
    }

    void latestIsTheLastTranscriptWithItsRecord()
    {
        ApplicationController controller(true);
        QuickInsights insights(&controller);
        QVERIFY(insights.latest().isNull());
        QSignalSpy latestChanged(&insights, &QuickInsights::latestChanged);

        DictationSession *session = controller.session();
        emit session->transcriptDelivered(QStringLiteral("Move the review to Thursday."));
        QVariantMap latest = insights.latest().toMap();
        QCOMPARE(latest.value(QStringLiteral("text")).toString(), QStringLiteral("Move the review to Thursday."));
        QCOMPARE(latest.value(QStringLiteral("words")).toInt(), 5);
        QVERIFY(latest.value(QStringLiteral("app")).toString().isEmpty());
        QVERIFY(latest.value(QStringLiteral("date")).isNull());

        DictationRecord record = recordOn(kToday, 5, QStringLiteral("Slack"), WritingProfile::Work);
        record.applicationId = QStringLiteral("com.slack.Slack");
        emit session->dictationFinished(finished(record), false);
        latest = insights.latest().toMap();
        QCOMPARE(latest.value(QStringLiteral("app")).toString(), QStringLiteral("Slack"));
        QCOMPARE(latest.value(QStringLiteral("appId")).toString(), QStringLiteral("com.slack.Slack"));
        QCOMPARE(latest.value(QStringLiteral("profile")).toString(), QStringLiteral("Work"));
        QCOMPARE(dateOf(latest.value(QStringLiteral("date"))), kToday);
        QVERIFY(latestChanged.count() >= 2);

        insights.copyLatest();
        QCOMPARE(QGuiApplication::clipboard()->text(), QStringLiteral("Move the review to Thursday."));
    }

    void latestProfileFollowsARename()
    {
        ApplicationController controller(true);
        AppSettings settings = controller.settings()->snapshot();
        settings.refinement.writingProfiles.append({QStringLiteral("custom_notes"), QStringLiteral("balanced"),
                                                    QStringLiteral("none"), QString(), QStringLiteral("Notes")});
        controller.settings()->applySnapshot(settings);
        QuickInsights insights(&controller);
        emit controller.session()->transcriptDelivered(QStringLiteral("Hello."));
        emit controller.session()->dictationFinished(
            finished(recordOn(kToday, 1, QStringLiteral("Kate"), QStringLiteral("custom_notes"))), false);
        QCOMPARE(insights.latest().toMap().value(QStringLiteral("profile")).toString(), QStringLiteral("Notes"));

        QSignalSpy latestChanged(&insights, &QuickInsights::latestChanged);
        settings.refinement.writingProfiles.last().name = QStringLiteral("Journal");
        controller.settings()->applySnapshot(settings);
        QCOMPARE(latestChanged.count(), 1);
        QCOMPARE(insights.latest().toMap().value(QStringLiteral("profile")).toString(), QStringLiteral("Journal"));
    }

    // A save that changes nothing Insights shows leaves it as it was.
    void anUnrelatedSaveChangesNothing()
    {
        ApplicationController controller(true);
        addStats(controller, recordOn(kToday, 40));
        QuickInsights insights(&controller);
        QSignalSpy changed(&insights, &QuickInsights::changed);
        QSignalSpy latestChanged(&insights, &QuickInsights::latestChanged);
        QSignalSpy correctionsChanged(&insights, &QuickInsights::correctionsChanged);

        QVERIFY(controller.settings()->edit([](AppSettings &draft) {
            draft.speech.language = QStringLiteral("fr");
            return true;
        }));

        QCOMPARE(changed.count(), 0);
        QCOMPARE(latestChanged.count(), 0);
        QCOMPARE(correctionsChanged.count(), 0);
    }

    void correctionsFollowWhatIsLearned()
    {
        ApplicationController controller(true);
        QuickInsights insights(&controller);
        QCOMPARE(insights.corrections().value(QStringLiteral("count")).toInt(), 0);
        QVERIFY(insights.corrections().value(QStringLiteral("learning")).toBool());

        QSignalSpy correctionsChanged(&insights, &QuickInsights::correctionsChanged);
        const CorrectionEvidence evidence{QStringLiteral("cute"), QStringLiteral("Qt"), 0.95};
        QVERIFY(controller.settings()->recordCorrectionEvidence(evidence, QStringLiteral("org.kde.kate")));
        QCOMPARE(correctionsChanged.count(), 1);
        const QVariantMap corrections = insights.corrections();
        QCOMPARE(corrections.value(QStringLiteral("count")).toInt(), 1);
        const QVariantMap recent = corrections.value(QStringLiteral("recent")).toList().value(0).toMap();
        QCOMPARE(recent.value(QStringLiteral("heard")).toString(), QStringLiteral("cute"));
        QCOMPARE(recent.value(QStringLiteral("fixed")).toString(), QStringLiteral("Qt"));
    }

    void jsonGoesToTheChosenFile()
    {
        ApplicationController controller(true);
        addStats(controller, recordOn(kToday, 120));
        QuickInsights insights(&controller);
        insights.setPeriod(QStringLiteral("7d"));

        QTemporaryDir folder;
        const QString path = folder.filePath(QStringLiteral("mine.json"));
        QVERIFY(insights.saveJson(QUrl::fromLocalFile(path)));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject json = QJsonDocument::fromJson(file.readAll()).object();
        QCOMPARE(json.value(QStringLiteral("today")).toString(), QStringLiteral("2026-09-26"));
        QCOMPARE(json.value(QStringLiteral("period")).toObject().value(QStringLiteral("name")).toString(),
                 QStringLiteral("last 7 days"));
        QCOMPARE(json.value(QStringLiteral("period")).toObject().value(QStringLiteral("words")).toInt(), 120);

        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("could not save")));
        QVERIFY(!insights.saveJson(QUrl::fromLocalFile(folder.filePath(QStringLiteral("missing/mine.json")))));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("came out empty")));
        QVERIFY(!insights.copyImage(QImage()));
    }

    void homeLoadsOverTheRealInsightsAndCopiesItsPicture()
    {
        failOnUnexpectedWarnings();
        ApplicationController controller(true);
        addStats(controller, recordOn(kToday.addDays(-2), 3033, QStringLiteral("Slack"), WritingProfile::Work));
        addStats(controller, recordOn(kToday, 120));
        emit controller.session()->transcriptDelivered(QStringLiteral("Hello there."));
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        QuickRecording recording(&controller);
        QuickNavigation navigation(nullptr);
        QuickInsights insights(&controller);
        QuickHistory history(&controller);
        // Home says what stands in the way of dictating: the shortcut, the
        // microphone, the speech service. Here a microphone is plugged in.
        test::SystemSingletons system(&controller);
        QuickAudio audio(&controller, nullptr, oneMicrophone);
        QuickModels models(&controller, &system.settings);
        QGuiApplication::clipboard()->clear();

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport Speecher.App\n"
                          "Window { width: 1100; height: 900; visible: true; property alias home: page\n"
                          "    HomePage { id: page; anchors.fill: parent } }",
                          QUrl());
        std::unique_ptr<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto *home = window->property("home").value<QQuickItem *>();
        QVERIFY(home);
        QVERIFY(QTest::qWaitForWindowExposed(qobject_cast<QWindow *>(window.get())));

        // The picture says Copied once it is on the clipboard, not before.
        auto *share = home->findChild<QObject *>(QStringLiteral("homeShare"));
        QVERIFY(share);
        QVERIFY(QMetaObject::invokeMethod(share, "picked", Q_ARG(QString, QStringLiteral("image"))));
        QVERIFY(share->property("confirmation").toString().isEmpty());
        QTRY_VERIFY(!QGuiApplication::clipboard()->image().isNull());
        QTRY_COMPARE(share->property("confirmation").toString(), QStringLiteral("Copied"));
        const QImage image = QGuiApplication::clipboard()->image();
        QVERIFY(image.width() >= 1400);
        // Drawn, not blank: paper, ink and the heatmap's levels.
        std::set<QRgb> colours;
        for (int y = 0; y < image.height(); y += 7) {
            for (int x = 0; x < image.width(); x += 7) {
                colours.insert(image.pixel(x, y));
            }
        }
        QVERIFY(colours.size() > 4);

        // Copy as text, worded by Home: the last 30 days.
        QVERIFY(QMetaObject::invokeMethod(share, "picked", Q_ARG(QString, QStringLiteral("text"))));
        QVERIFY(QGuiApplication::clipboard()->text().startsWith(
            QStringLiteral("My Speecher stats, last 30 days\n3,153 words dictated\nAbout")));

        // The Words note's tooltip: the book's length and the next count
        // that compares differently.
        QCOMPARE(figureTips(home), QStringList{QStringLiteral("The Declaration of Independence is about 1,321 words\n"
                                                             "Changes to about as long as An Occurrence at Owl Creek "
                                                             "Bridge at 3,154 words")});

        // The best run, Thursday's, isn't the one going on today.
        QVERIFY(findText(home, QStringLiteral("Longest streak")));
        QVERIFY(findText(home, QStringLiteral("Ended Thursday")));
    }

    // With no microphone Home is not ready: it lists the microphone among
    // what dictation needs, hides Start dictation, and the row opens the
    // Microphone page.
    void homeListsWhatStopsDictation()
    {
        failOnUnexpectedWarnings();
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        QuickRecording recording(&controller);
        QuickNavigation navigation(nullptr);
        QuickInsights insights(&controller);
        QuickHistory history(&controller);
        test::SystemSingletons system(&controller);
        QuickAudio audio(&controller, nullptr, [] { return QList<AudioInputDeviceInfo>{}; });
        QuickModels models(&controller, &system.settings);

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport Speecher.App\n"
                          "Window { width: 1100; height: 900; visible: true; property alias home: page\n"
                          "    HomePage { id: page; anchors.fill: parent } }",
                          QUrl());
        std::unique_ptr<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto *home = window->property("home").value<QQuickItem *>();
        QVERIFY(QTest::qWaitForWindowExposed(qobject_cast<QWindow *>(window.get())));
        auto *session = home->findChild<QObject *>(QStringLiteral("homeSession"));
        QVERIFY(session);
        auto *readiness = session->findChild<QObject *>(QStringLiteral("readiness"));
        QVERIFY(readiness);
        QVERIFY(readiness->property("blocked").toBool());
        QCOMPARE(session->property("title").toString(), QStringLiteral("Not ready to dictate"));
        // Every check is listed, in its group: the shortcut is what
        // dictation works without. Microphone access isn't a row of its own
        // but the Microphone row's, which says what is missing.
        QVERIFY(readiness->property("neededChecks").value<QJSValue>().toVariant().toStringList().isEmpty());
        QVERIFY(readiness->property("optionalChecks").value<QJSValue>().toVariant().toStringList().contains(QStringLiteral("globalShortcuts")));
        const QVariantMap microphone = readiness->property("microphoneItem").value<QJSValue>().toVariant().toMap();
        QCOMPARE(microphone.value(QStringLiteral("state")).toString(), QStringLiteral("missing"));

        // Delegates hang off the visual tree, not the object tree.
        QQuickItem *row = nullptr;
        QList<QQuickItem *> items{home};
        while (!items.isEmpty() && !row) {
            QQuickItem *item = items.takeLast();
            items += item->childItems();
            if (item->objectName() == QLatin1String("readiness_microphone")) row = item;
        }
        QVERIFY(row);
        QVERIFY(QMetaObject::invokeMethod(row, "clicked"));
        QCOMPARE(navigation.current(), QStringLiteral("microphone"));
    }

    // With no service in the speech chain working, Home is not ready and
    // says it needs one, but still offers Start dictation, to try again.
    void homeIsNotReadyWithoutAWorkingSpeechService()
    {
        failOnUnexpectedWarnings();
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        QuickRecording recording(&controller);
        QuickNavigation navigation(nullptr);
        QuickInsights insights(&controller);
        QuickHistory history(&controller);
        test::SystemSingletons system(&controller);
        QuickAudio audio(&controller, nullptr, oneMicrophone);
        QuickModels models(&controller, &system.settings);

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport Speecher.App\n"
                          "Window { width: 1100; height: 900; property alias home: page\n"
                          "    HomePage { id: page; anchors.fill: parent } }",
                          QUrl());
        std::unique_ptr<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto *session = window->property("home").value<QQuickItem *>()->findChild<QObject *>(QStringLiteral("homeSession"));
        QVERIFY(session);
        auto *readiness = session->findChild<QObject *>(QStringLiteral("readiness"));
        QVERIFY(readiness);
        // Home asked for the sign-ins; the test's homes have none.
        QTRY_COMPARE(readiness->property("speechState").toString(), QStringLiteral("attention"));
        QVERIFY(readiness->property("blocked").toBool());
        QVERIFY(!readiness->property("cantStart").toBool());
        QCOMPARE(session->property("title").toString(), QStringLiteral("Not ready to dictate"));
        QCOMPARE(session->property("caption").toString(), QStringLiteral("Dictation needs a working speech service."));
    }

    // A speech service nobody has checked yet isn't ready: Home says so in
    // its title and lists it, while Start dictation stays, to try it.
    void homeCountsAnUncheckedSpeechServiceAsOutstanding()
    {
        failOnUnexpectedWarnings();
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("speechServices"), test::serviceOrder({QStringLiteral("endpoint")}));
        settings.set(QStringLiteral("speechEndpointUrl"), QStringLiteral("http://127.0.0.1:9/v1"));
        QuickDictation dictation(&controller);
        QuickRecording recording(&controller);
        QuickNavigation navigation(nullptr);
        QuickInsights insights(&controller);
        QuickHistory history(&controller);
        test::SystemSingletons system(&controller);
        QuickAudio audio(&controller, nullptr, oneMicrophone);
        QuickModels models(&controller, &system.settings);

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport Speecher.App\n"
                          "Window { width: 1100; height: 900; property alias home: page\n"
                          "    HomePage { id: page; anchors.fill: parent } }",
                          QUrl());
        std::unique_ptr<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto *session = window->property("home").value<QQuickItem *>()->findChild<QObject *>(QStringLiteral("homeSession"));
        QVERIFY(session);
        auto *readiness = session->findChild<QObject *>(QStringLiteral("readiness"));
        QVERIFY(readiness);
        QCOMPARE(readiness->property("speechState").toString(), QStringLiteral("notChecked"));
        QVERIFY(!readiness->property("allPass").toBool());
        QVERIFY(!readiness->property("cantStart").toBool());
        QCOMPARE(session->property("title").toString(), QStringLiteral("Speech to text isn't checked yet"));
        QCOMPARE(session->property("caption").toString(), QStringLiteral("Start a dictation to try it."));
    }

    void learnedCorrectionsOpenTheirRows()
    {
        failOnUnexpectedWarnings();
        ApplicationController controller(true);
        addStats(controller, recordOn(kToday, 120));
        QVERIFY(controller.settings()->recordCorrectionEvidence({QStringLiteral("cute"), QStringLiteral("Qt"), 0.95},
                                                               QStringLiteral("org.kde.kate")));
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        QuickRecording recording(&controller);
        QuickNavigation navigation(nullptr);
        QuickInsights insights(&controller);
        QuickHistory history(&controller);
        // Home says what stands in the way of dictating: the shortcut, the
        // microphone, the speech service. Here a microphone is plugged in.
        test::SystemSingletons system(&controller);
        QuickAudio audio(&controller, nullptr, oneMicrophone);
        QuickModels models(&controller, &system.settings);

        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport Speecher.App\n"
                          "Window { width: 1100; height: 900; property alias home: page\n"
                          "    HomePage { id: page; anchors.fill: parent } }",
                          QUrl());
        std::unique_ptr<QObject> window(component.create());
        QVERIFY2(window, qPrintable(component.errorString()));
        auto *home = window->property("home").value<QQuickItem *>();
        auto *action = home->findChild<QObject *>(QStringLiteral("learnedCorrectionsAction"));
        QVERIFY(action);

        QCOMPARE(action->property("text").toString(), QStringLiteral("Review learned corrections"));
        QVERIFY(QMetaObject::invokeMethod(action, "clicked"));
        QCOMPARE(navigation.current(), QStringLiteral("vocabulary"));
        QCOMPARE(navigation.pendingRow(), QStringLiteral("learnedCorrections"));

        AppSettings off = controller.settings()->snapshot();
        off.correctionLearningEnabled = false;
        controller.settings()->applySnapshot(off);
        QCOMPARE(action->property("text").toString(), QStringLiteral("Turn on learning"));
        QVERIFY(QMetaObject::invokeMethod(action, "clicked"));
        QCOMPARE(navigation.pendingRow(), QStringLiteral("correctionLearningControl"));
    }
};

int runQuickInsightsTests(int argc, char **argv)
{
    QuickInsightsTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_insights.moc"
