// The whole Quick UI as the daemon shows it, over the real Backend, in light
// and dark: every page of Main and a few searches, every setup step and every
// popup state load and draw without a QML warning.
#include "common/test_quick_windows.h"
#include "common/test_suites.h"

#include "app/AppFrontEnd.h"
#include "app/ApplicationController.h"
#include "app/TrayIcon.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "dictation/PopupPresentation.h"
#include "dictation/SelectionEditPresentation.h"
#include "frontend/quick/host/QuickFrontEnd.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QGuiApplication>
#include <QJSValue>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>

#include <algorithm>
#include <functional>
#include <utility>

using namespace speecher;
using speecher::test::openWindow;

namespace {

// Long enough for a page to load and its entrance animations to finish.
constexpr int kSettleMs = 400;

// Whether the Theme that window draws with is dark.
bool themeIsDark(QQuickWindow *window)
{
    QObject *theme = qmlEngine(window)->singletonInstance<QObject *>(QStringLiteral("Speecher.Theme"), QStringLiteral("Theme"));
    return theme && theme->property("dark").toBool();
}

// Runs each step and lets the window settle after it, so a warning a step
// causes fails the test there; the log names the step.
void runSteps(const QList<std::pair<QString, std::function<void()>>> &steps)
{
    for (const auto &[name, step] : steps) {
        qInfo().noquote() << "step:" << name;
        step();
        QTest::qWait(kSettleMs);
    }
}

// The search palette's results for query: { id, trail } of each.
QList<std::pair<QString, QString>> searchResults(QObject *search, const QString &query)
{
    search->setProperty("query", query);
    QList<std::pair<QString, QString>> results;
    for (const QVariant &result : search->property("results").value<QJSValue>().toVariant().toList()) {
        const QVariantMap map = result.toMap();
        results.append({map.value(QStringLiteral("id")).toString(), map.value(QStringLiteral("trail")).toString()});
    }
    return results;
}

// The sidebar entry for pageId in window, or null.
QQuickItem *sidebarEntry(QQuickWindow *window, const QString &pageId)
{
    QList<QQuickItem *> items{window->contentItem()};
    while (!items.isEmpty()) {
        QQuickItem *item = items.takeLast();
        items += item->childItems();
        QVariant page = item->property("page");
        if (page.canConvert<QJSValue>()) {
            page = page.value<QJSValue>().toVariant();
        }
        if (item->property("hasSubpages").isValid() && page.toMap().value(QStringLiteral("id")).toString() == pageId) {
            return item;
        }
    }
    return nullptr;
}

} // namespace

class QuickPagesTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Frames without a GPU, so a shown window really renders one.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
        // QuickFrontEnd logs every warning the engine reports as "QML: …".
        // The rest are what the offscreen platform lacks: a tray, layer shell
        // and window masks.
        QTest::failOnWarning(QRegularExpression(QStringLiteral("^QML:")));
    }

    void everyPageAndSearchLoads_data() { themes(); }
    void everyPageAndSearchLoads()
    {
        QFETCH(QString, theme);
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("themeControl"), theme);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        QCOMPARE(themeIsDark(main), theme == QLatin1String("dark"));
        QQmlEngine *engine = qmlEngine(main);
        auto *navigation = engine->singletonInstance<QObject *>(QStringLiteral("Speecher.App"), QStringLiteral("Navigation"));
        auto *schema = engine->singletonInstance<QObject *>(QStringLiteral("Speecher.Wording"), QStringLiteral("Schema"));
        auto *search = main->findChild<QObject *>(QStringLiteral("searchPalette"));
        QVERIFY(navigation && schema && search);
        const QVariantList pages = schema->property("pages").value<QJSValue>().toVariant().toList();
        QVERIFY(pages.size() > 10);

        QList<std::pair<QString, std::function<void()>>> steps;
        for (const QVariant &page : pages) {
            const QString id = page.toMap().value(QStringLiteral("id")).toString();
            steps.append({QStringLiteral("page ") + id, [navigation, id] {
                              navigation->setProperty("current", id);
                              QCOMPARE(navigation->property("current").toString(), id);
                          }});
        }
        // Rows moved to a subpage land there.
        for (const auto &[row, page] : {std::pair{QStringLiteral("themeControl"), QStringLiteral("appearance")},
                                        std::pair{QStringLiteral("microphoneAccess"), QStringLiteral("permissions")},
                                        std::pair{QStringLiteral("appRecognitionRules"), QStringLiteral("appRules")}}) {
            steps.append({QStringLiteral("row ") + row, [navigation, row, page] {
                              QMetaObject::invokeMethod(navigation, "goToRow", Q_ARG(QVariant, row));
                              QCOMPARE(navigation->property("current").toString(), page);
                          }});
        }
        for (const QString query : {QStringLiteral("shortcut"), QStringLiteral("api key"), QStringLiteral("paste"), QStringLiteral("refinement"),
                                    QStringLiteral("zzz no match")}) {
            steps.append({QStringLiteral("search ") + query, [search, query] {
                              QMetaObject::invokeMethod(search, "open");
                              search->setProperty("query", query);
                          }});
        }
        // A parent's title finds its subpages, and they and their rows show
        // the path to them.
        steps.append({QStringLiteral("search paths"), [search] {
                          QMetaObject::invokeMethod(search, "open");
                          const auto parent = searchResults(search, QStringLiteral("refinement"));
                          QVERIFY(parent.contains(std::pair{QStringLiteral("profiles"), QStringLiteral("Refinement › Writing profiles")}));
                          QVERIFY(parent.contains(std::pair{QStringLiteral("vocabulary"), QStringLiteral("Refinement › Vocabulary")}));
                          const auto child = searchResults(search, QStringLiteral("spelling"));
                          QVERIFY(std::any_of(child.cbegin(), child.cend(), [](const auto &result) {
                              return result.second == QStringLiteral("Refinement › Vocabulary, Terms");
                          }));
                      }});
        steps.append({QStringLiteral("close search"), [search] { QMetaObject::invokeMethod(search, "close"); }});
        runSteps(steps);
    }

    // Closing a group hands a subpage's keyboard focus to its parent, and
    // asking for a page whose group was closed by hand opens it again, even
    // when that page is already showing.
    void sidebarGroupsFollowRequestsAndFocus()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QObject *>(QStringLiteral("Speecher.App"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        QMetaObject::invokeMethod(navigation, "go", Q_ARG(QVariant, QStringLiteral("profiles")));
        QQuickItem *refinement = sidebarEntry(main, QStringLiteral("refinement"));
        QQuickItem *profiles = sidebarEntry(main, QStringLiteral("profiles"));
        QVERIFY(refinement && profiles);
        QTRY_VERIFY(refinement->property("open").toBool());
        QTest::qWait(kSettleMs);

        profiles->forceActiveFocus(Qt::TabFocusReason);
        QVERIFY(profiles->hasActiveFocus());
        QMetaObject::invokeMethod(refinement, "openRequested", Q_ARG(bool, false));
        QVERIFY(!refinement->property("open").toBool());
        QVERIFY(refinement->hasActiveFocus());

        QMetaObject::invokeMethod(navigation, "go", Q_ARG(QVariant, QStringLiteral("profiles")));
        QVERIFY(refinement->property("open").toBool());

        QMetaObject::invokeMethod(refinement, "openRequested", Q_ARG(bool, false));
        QMetaObject::invokeMethod(navigation, "goToRow", Q_ARG(QVariant, QStringLiteral("defaultWritingProfile")));
        QVERIFY(refinement->property("open").toBool());
        QCOMPARE(navigation->property("current").toString(), QStringLiteral("profiles"));
    }

    void everySetupStepLoads_data() { themes(); }
    void everySetupStepLoads()
    {
        QFETCH(QString, theme);
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("themeControl"), theme);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showSetupAssistant(SetupAssistantPage::All);
        QQuickWindow *setup = openWindow("SetupAssistant");
        QVERIFY(setup);
        QVERIFY(QTest::qWaitForWindowExposed(setup));
        QCOMPARE(themeIsDark(setup), theme == QLatin1String("dark"));
        const int count = setup->property("steps").value<QJSValue>().property(QStringLiteral("length")).toInt();
        QVERIFY(count > 3);

        QList<std::pair<QString, std::function<void()>>> steps;
        for (int step = 0; step < count; ++step) {
            steps.append({QStringLiteral("step %1").arg(step), [setup, step] { setup->setProperty("step", step); }});
        }
        runSteps(steps);
    }

    void everyPopupStateLoads_data() { themes(); }
    void everyPopupStateLoads()
    {
        QFETCH(QString, theme);
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("themeControl"), theme);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        DictationSession *session = controller.session();
        emit session->stateChanged(QStringLiteral("starting"));
        emit session->popupShowRequested(1);
        QQuickWindow *popup = openWindow("PopupWindow");
        QVERIFY(popup);
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        QCOMPARE(themeIsDark(popup), theme == QLatin1String("dark"));

        const auto review = [session](bool following) {
            SelectionEditReview edit = selectionEditReview(
                QStringLiteral("Can you send me the deck when you get a chance?"),
                QStringLiteral("Can you send me the slides when you can?"), {QStringLiteral("make it shorter")},
                reviewKeysFor(true, true, QString(), QStringLiteral("Ctrl+Space")));
            edit.following = following;
            emit session->stateChanged(QStringLiteral("reviewing"));
            emit session->popupSelectionEditReviewRequested(edit);
        };
        runSteps({
            {QStringLiteral("starting"), [] {}},
            {QStringLiteral("renewing"), [session] { emit session->popupOAuthRefreshRequested(); }},
            {QStringLiteral("listening"),
             [session] {
                 emit session->stateChanged(QStringLiteral("listening"));
                 emit session->audioLevelChanged(0.6f);
                 emit session->previewDisplayChanged(QStringLiteral("Quick update on the launch"));
             }},
            {QStringLiteral("paused"), [session] { emit session->stateChanged(QStringLiteral("paused")); }},
            {QStringLiteral("transcribing"), [session] { emit session->stateChanged(QStringLiteral("stopping")); }},
            {QStringLiteral("refining"),
             [session] {
                 emit session->stateChanged(QStringLiteral("refining"));
                 emit session->popupRefinementPreviewChanged(QStringLiteral("Quick update on the launch."));
             }},
            {QStringLiteral("delivering"), [session] { emit session->stateChanged(QStringLiteral("delivering")); }},
            {QStringLiteral("delivered"),
             [session] { emit session->popupMessageRequested(QStringLiteral("Input sent"), PopupOutcome::Inserted); }},
            {QStringLiteral("review"), [review] { review(false); }},
            {QStringLiteral("following"), [review] { review(true); }},
            {QStringLiteral("error"),
             [session] {
                 emit session->stateChanged(QStringLiteral("error"));
                 emit session->popupErrorRequested(QStringLiteral("Couldn't reach Claude • Check your connection."),
                                                   PopupErrorAction{});
             }},
        });
    }

private:
    static void themes()
    {
        QTest::addColumn<QString>("theme");
        QTest::newRow("light") << QStringLiteral("light");
        QTest::newRow("dark") << QStringLiteral("dark");
    }
};

int runQuickPagesTests(int argc, char **argv)
{
    QuickPagesTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_pages.moc"
