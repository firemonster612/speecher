// The whole Quick UI as the daemon shows it, over the real Backend, in light
// and dark: every page of Main and a few searches, every setup step and every
// popup state load and draw without a QML warning.
#include "common/test_quick_windows.h"
#include "common/test_suites.h"

#include "app/AppFrontEnd.h"
#include "app/ApplicationController.h"
#include "app/TrayIcon.h"
#include "core/SettingsStore.h"
#include "core/settings/SpokenLanguages.h"
#include "dictation/DictationSession.h"
#include "dictation/PopupPresentation.h"
#include "dictation/SelectionEditPresentation.h"
#include "frontend/quick/host/QuickFrontEnd.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QGuiApplication>
#include <QJSValue>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>

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
        for (const QString query : {QStringLiteral("shortcut"), QStringLiteral("api key"), QStringLiteral("paste"),
                                    QStringLiteral("zzz no match")}) {
            steps.append({QStringLiteral("search ") + query, [search, query] {
                              QMetaObject::invokeMethod(search, "open");
                              search->setProperty("query", query);
                          }});
        }
        steps.append({QStringLiteral("close search"), [search] { QMetaObject::invokeMethod(search, "close"); }});
        // The Spoken language's picker opens, filters and closes.
        steps.append({QStringLiteral("language picker"), [navigation, main] {
                          navigation->setProperty("current", QStringLiteral("speech"));
                          QTest::qWait(kSettleMs);
                          QObject *picker = main->findChild<QObject *>(QStringLiteral("languageSelect_spokenLanguage"));
                          QVERIFY(picker);
                          QMetaObject::invokeMethod(picker, "click");
                      }});
        steps.append({QStringLiteral("search languages"), [main] {
                          for (const char key : {'d', 'e', 'u'}) {
                              QTest::keyClick(main, key);
                          }
                      }});
        steps.append({QStringLiteral("close language picker"), [main] { QTest::keyClick(main, Qt::Key_Escape); }});
        runSteps(steps);
    }

    // The Spoken language row names every language core knows, which the
    // picker and the lists of a service's languages show.
    void everyKnownLanguageHasAName()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        auto *schema = qmlEngine(main)->singletonInstance<QObject *>(QStringLiteral("Speecher.Wording"), QStringLiteral("Schema"));
        QVERIFY(schema);
        QVariant row;
        QVERIFY(QMetaObject::invokeMethod(schema, "row", Q_RETURN_ARG(QVariant, row),
                                          Q_ARG(QVariant, QStringLiteral("spokenLanguage"))));
        QStringList named;
        for (const QVariant &option : row.value<QJSValue>().toVariant().toMap().value(QStringLiteral("options")).toList()) {
            named.append(option.toMap().value(QStringLiteral("id")).toString());
        }
        named.sort();
        QCOMPARE(named, knownSpokenLanguages());
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
