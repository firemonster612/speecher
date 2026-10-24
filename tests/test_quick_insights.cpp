#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/InsightsLog.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "frontend/quick/host/QuickInsights.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/QuickStatus.h"
#include "frontend/quick/host/QuickDictation.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
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

QString logPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/insights.jsonl");
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
        QFile::remove(logPath());
    }

    void cleanup()
    {
        QDir(logPath()).removeRecursively();
        QFile::remove(logPath());
    }

    void stateFollowsTheLogAndKeepInsights()
    {
        ApplicationController controller(true);
        QuickInsights insights(&controller);
        QCOMPARE(insights.state(), QStringLiteral("empty"));
        QVERIFY(insights.summary().isNull());
        QVERIFY(insights.firstDictation().isNull());

        QSignalSpy changed(&insights, &QuickInsights::changed);
        controller.insightsLog()->append(recordOn(kToday.addDays(-3), 40));
        QCOMPARE(insights.state(), QStringLiteral("ready"));
        QCOMPARE(insights.kept(), 1);
        QCOMPARE(dateOf(insights.firstDictation()), kToday.addDays(-3));
        QVERIFY(changed.count() >= 1);

        // Turned off, nothing shows, and what was kept stays until cleared.
        AppSettings settings = controller.settings()->snapshot();
        settings.insightsEnabled = false;
        controller.settings()->applySnapshot(settings);
        QCOMPARE(insights.state(), QStringLiteral("off"));
        QVERIFY(!insights.hasData());
        QVERIFY(insights.summary().isNull());
        QCOMPARE(insights.kept(), 1);
    }

    void summaryCarriesCoresNumbersForThePeriod()
    {
        ApplicationController controller(true);
        controller.insightsLog()->append(recordOn(kToday.addDays(-70), 500, QStringLiteral("Slack"), WritingProfile::Work));
        controller.insightsLog()->append(recordOn(kToday.addDays(-1), 300, QStringLiteral("Slack"), WritingProfile::Work));
        controller.insightsLog()->append(recordOn(kToday, 100, QStringLiteral("Kate"), WritingProfile::Email));
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
        controller.insightsLog()->append(recordOn(kToday.addDays(-1), 900));
        for (int i = 0; i < 3; ++i) {
            controller.insightsLog()->append(recordOn(kToday, 10));
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
        controller.insightsLog()->append(recordOn(kToday, 10));
        QuickInsights insights(&controller);
        QVERIFY(QFile::exists(logPath()));

        // A non-empty directory where the log file was: it cannot be deleted.
        QVERIFY(QFile::remove(logPath()));
        QVERIFY(QDir().mkpath(logPath() + QStringLiteral("/keep")));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("could not be deleted")));
        QVERIFY(!insights.clear());
        QCOMPARE(insights.kept(), 1);
        QCOMPARE(insights.state(), QStringLiteral("ready"));

        QVERIFY(QDir(logPath()).removeRecursively());
        QVERIFY(insights.clear());
        QCOMPARE(insights.kept(), 0);
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

        emit session->dictationRecorded(recordOn(kToday, 5, QStringLiteral("Slack"), WritingProfile::Work));
        latest = insights.latest().toMap();
        QCOMPARE(latest.value(QStringLiteral("app")).toString(), QStringLiteral("Slack"));
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
        emit controller.session()->dictationRecorded(
            recordOn(kToday, 1, QStringLiteral("Kate"), QStringLiteral("custom_notes")));
        QCOMPARE(insights.latest().toMap().value(QStringLiteral("profile")).toString(), QStringLiteral("Notes"));

        QSignalSpy latestChanged(&insights, &QuickInsights::latestChanged);
        settings.refinement.writingProfiles.last().name = QStringLiteral("Journal");
        controller.settings()->applySnapshot(settings);
        QCOMPARE(latestChanged.count(), 1);
        QCOMPARE(insights.latest().toMap().value(QStringLiteral("profile")).toString(), QStringLiteral("Journal"));
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
        controller.insightsLog()->append(recordOn(kToday, 120));
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
        QTest::failOnWarning();
        ApplicationController controller(true);
        controller.insightsLog()->append(recordOn(kToday.addDays(-2), 300, QStringLiteral("Slack"), WritingProfile::Work));
        controller.insightsLog()->append(recordOn(kToday, 120));
        emit controller.session()->transcriptDelivered(QStringLiteral("Hello there."));
        QuickSettings settings(&controller);
        QuickStatus status(&controller);
        QuickDictation dictation(&controller);
        QuickInsights insights(&controller);
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
            QStringLiteral("My Speecher stats, last 30 days\n420 words dictated\nAbout")));

        // The best run, Thursday's, isn't the one going on today.
        QVERIFY(findText(home, QStringLiteral("Longest streak")));
        QVERIFY(findText(home, QStringLiteral("Ended Thursday")));
    }

    void learnedCorrectionsOpenTheirRows()
    {
        QTest::failOnWarning();
        ApplicationController controller(true);
        controller.insightsLog()->append(recordOn(kToday, 120));
        QVERIFY(controller.settings()->recordCorrectionEvidence({QStringLiteral("cute"), QStringLiteral("Qt"), 0.95},
                                                               QStringLiteral("org.kde.kate")));
        QuickSettings settings(&controller);
        QuickStatus status(&controller);
        QuickDictation dictation(&controller);
        QuickInsights insights(&controller);

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
        auto *navigation = engine.singletonInstance<QObject *>("Speecher.App", "Navigation");
        QVERIFY(navigation);

        QCOMPARE(action->property("text").toString(), QStringLiteral("Review learned corrections"));
        QVERIFY(QMetaObject::invokeMethod(action, "clicked"));
        QCOMPARE(navigation->property("current").toString(), QStringLiteral("vocabulary"));
        QCOMPARE(navigation->property("pendingRow").toString(), QStringLiteral("learnedCorrections"));

        AppSettings off = controller.settings()->snapshot();
        off.correctionLearningEnabled = false;
        controller.settings()->applySnapshot(off);
        QCOMPARE(action->property("text").toString(), QStringLiteral("Turn on learning"));
        QVERIFY(QMetaObject::invokeMethod(action, "clicked"));
        QCOMPARE(navigation->property("pendingRow").toString(), QStringLiteral("correctionLearningControl"));
    }
};

int runQuickInsightsTests(int argc, char **argv)
{
    QuickInsightsTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_insights.moc"
