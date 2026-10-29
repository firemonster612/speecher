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
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>

#include <algorithm>
#include <functional>
#include <memory>
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
// causes fails the test there; the log names the step. A step that fails
// stops the rest, which may rely on what it found.
void runSteps(const QList<std::pair<QString, std::function<void()>>> &steps)
{
    for (const auto &[name, step] : steps) {
        qInfo().noquote() << "step:" << name;
        step();
        if (QTest::currentTestFailed()) {
            return;
        }
        QTest::qWait(kSettleMs);
    }
}

// The item named name under root, by the visual tree, which also holds
// what a Repeater made.
QQuickItem *findItem(QQuickItem *root, const QString &name)
{
    for (QQuickItem *child : root->childItems()) {
        if (child->objectName() == name) {
            return child;
        }
        if (QQuickItem *found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
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
                          QVERIFY(searchResults(search, QStringLiteral("shortcuts"))
                                      .contains(std::pair{QStringLiteral("shortcuts"), QStringLiteral("Dictation › Shortcuts")}));
                          QVERIFY(searchResults(search, QStringLiteral("accounts"))
                                      .contains(std::pair{QStringLiteral("accounts"), QStringLiteral("General › Accounts")}));
                      }});
        steps.append({QStringLiteral("close search"), [search] { QMetaObject::invokeMethod(search, "close"); }});
        runSteps(steps);
    }

    // The Spoken language's picker opens with every language, narrows as you
    // type by English name, own name or code, closes on Escape and chooses
    // with the arrows and Return. A service card's count of languages opens
    // the same list of only that service's, without choosing the card, and
    // it stays open while Models' changes make the cards again.
    void languagePickerChoosesAndShowsLanguages_data() { themes(); }
    void languagePickerChoosesAndShowsLanguages()
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
        auto *navigation = qmlEngine(main)->singletonInstance<QObject *>(QStringLiteral("Speecher.App"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        const auto language = [&controller] { return controller.settings()->snapshot().speech.language; };
        const auto service = [&controller] { return controller.settings()->snapshot().speech.providerId; };
        QCOMPARE(language(), QStringLiteral("en"));

        QObject *picker = nullptr;
        QObject *popover = nullptr;
        QObject *search = nullptr;
        QObject *rows = nullptr;
        // The label of each row the popover shows.
        const auto shown = [&rows] {
            QStringList labels;
            for (int i = 0; i < rows->property("count").toInt(); ++i) {
                QQuickItem *row = nullptr;
                QMetaObject::invokeMethod(rows, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, i));
                labels.append(row ? row->property("text").toString() : QString());
            }
            return labels;
        };
        const auto type = [main](const char *text) {
            for (const char *key = text; *key; ++key) {
                QTest::keyClick(main, *key);
            }
        };
        QPointer<QQuickItem> codexHelp;
        int hovered = -1;
        QObject *serviceLanguages = nullptr;

        runSteps({
            {QStringLiteral("speech page"), [navigation] { navigation->setProperty("current", QStringLiteral("speech")); }},
            {QStringLiteral("open the picker"), [&] {
                 picker = main->findChild<QObject *>(QStringLiteral("languageSelect_spokenLanguage"));
                 QVERIFY(picker);
                 popover = picker->findChild<QObject *>(QStringLiteral("languagePopover"));
                 QVERIFY(popover);
                 QVERIFY(!popover->property("opened").toBool());
                 QVERIFY(QMetaObject::invokeMethod(picker, "click"));
             }},
            {QStringLiteral("type a language's own name"), [&] {
                 QTRY_VERIFY(popover->property("opened").toBool());
                 search = popover->findChild<QObject *>(QStringLiteral("languageSearch"));
                 rows = popover->findChild<QObject *>(QStringLiteral("languageRows"));
                 QVERIFY(search && rows);
                 QCOMPARE(rows->property("count").toInt(), knownSpokenLanguages().size());
                 type("deutsch");
             }},
            {QStringLiteral("search by English name and code"), [&] {
                 QCOMPARE(search->property("text").toString(), QStringLiteral("deutsch"));
                 QCOMPARE(shown(), QStringList{QStringLiteral("German (Deutsch)")});
                 search->setProperty("text", QStringLiteral("german"));
                 QCOMPARE(shown(), QStringList{QStringLiteral("German (Deutsch)")});
                 search->setProperty("text", QStringLiteral("yue"));
                 QCOMPARE(shown(), QStringList{QStringLiteral("Cantonese (粵語)")});
                 QTest::keyClick(main, Qt::Key_Escape);
             }},
            {QStringLiteral("reopen after Escape"), [&] {
                 QTRY_VERIFY(!popover->property("opened").toBool());
                 QCOMPARE(language(), QStringLiteral("en"));
                 QVERIFY(QMetaObject::invokeMethod(picker, "click"));
             }},
            {QStringLiteral("choose with the keys"), [&] {
                 QTRY_VERIFY(popover->property("opened").toBool());
                 QCOMPARE(search->property("text").toString(), QString());
                 // From English, two down is Faroese.
                 QTest::keyClick(main, Qt::Key_Down);
                 QTest::keyClick(main, Qt::Key_Down);
                 QTest::keyClick(main, Qt::Key_Return);
             }},
            {QStringLiteral("click ChatGPT's count of languages"), [&] {
                 QTRY_VERIFY(!popover->property("opened").toBool());
                 QCOMPARE(language(), QStringLiteral("fo"));
                 QCOMPARE(picker->property("currentId").toString(), QStringLiteral("fo"));

                 codexHelp = findItem(main->contentItem(), QStringLiteral("help_codex"));
                 serviceLanguages = main->findChild<QObject *>(QStringLiteral("serviceLanguages"));
                 QVERIFY(codexHelp);
                 QVERIFY(serviceLanguages);
                 // Where along its first line the link is.
                 const qreal y = codexHelp->height() / 4;
                 qreal x = 0;
                 for (; x < codexHelp->width(); x += 2) {
                     QString link;
                     QMetaObject::invokeMethod(codexHelp, "linkAt", Q_RETURN_ARG(QString, link), Q_ARG(qreal, x), Q_ARG(qreal, y));
                     if (!link.isEmpty()) break;
                 }
                 QVERIFY(x < codexHelp->width());
                 QVERIFY(service() != QStringLiteral("codex"));
                 QTest::mouseClick(main, Qt::LeftButton, Qt::NoModifier, codexHelp->mapToScene(QPointF(x + 4, y)).toPoint());
             }},
            {QStringLiteral("make the cards again"), [&] {
                 QTRY_VERIFY(serviceLanguages->property("opened").toBool());
                 QVERIFY(service() != QStringLiteral("codex"));
                 auto *codexRows = serviceLanguages->findChild<QObject *>(QStringLiteral("languageRows"));
                 QVERIFY(codexRows);
                 QCOMPARE(codexRows->property("count").toInt(), 67);
                 // Claude listens for English but not Faroese, so its state
                 // changes and Models' change makes the cards again.
                 QuickSettings(&controller).set(QStringLiteral("spokenLanguage"), QStringLiteral("en"));
             }},
            {QStringLiteral("press another card"), [&] {
                 QTRY_VERIFY(!codexHelp);
                 QVERIFY(serviceLanguages->property("opened").toBool());
                 QQuickItem *claudeHelp = findItem(main->contentItem(), QStringLiteral("help_claude"));
                 QVERIFY(claudeHelp);
                 QQuickItem *claudeCard = claudeHelp->parentItem()->parentItem();
                 QTest::mouseClick(main, Qt::LeftButton, Qt::NoModifier,
                                   claudeCard->mapToScene(QPointF(claudeCard->width() - 12, claudeCard->height() - 12)).toPoint());
             }},
            {QStringLiteral("rest the pointer on a row of the picker"), [&] {
                 QTRY_VERIFY(!serviceLanguages->property("opened").toBool());
                 QVERIFY(QMetaObject::invokeMethod(picker, "click"));
             }},
            {QStringLiteral("arrow down under the still pointer"), [&] {
                 QTRY_VERIFY(popover->property("opened").toBool());
                 // The first row wholly in view, found from the rows rather
                 // than a point, which can fall between two rows.
                 int top = -1;
                 QQuickItem *row = nullptr;
                 const auto findRow = [&] {
                     const qreal contentY = rows->property("contentY").toReal();
                     top = -1;
                     for (int i = 0; top < 0 && i < rows->property("count").toInt(); ++i) {
                         QQuickItem *item = nullptr;
                         QMetaObject::invokeMethod(rows, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item), Q_ARG(int, i));
                         if (item && item->y() >= contentY) {
                             top = i;
                         }
                     }
                     hovered = top + 2;
                     row = nullptr;
                     QMetaObject::invokeMethod(rows, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, row), Q_ARG(int, hovered));
                     return top >= 0 && row;
                 };
                 QTRY_VERIFY2(findRow(), qPrintable(QStringLiteral("top %1, contentY %2, count %3, height %4")
                                                        .arg(top)
                                                        .arg(rows->property("contentY").toReal())
                                                        .arg(rows->property("count").toInt())
                                                        .arg(rows->property("height").toReal())));
                 // The pointer comes onto the row, then rests there.
                 const QPointF centre = row->mapToScene(QPointF(row->width() / 2, row->height() / 2));
                 QTest::mouseMove(main, (centre - QPointF(0, 3)).toPoint());
                 QTest::mouseMove(main, centre.toPoint());
                 // A frame after each, as on screen, so the list scrolls under
                 // the pointer between presses.
                 for (int i = 0; i < 12; ++i) {
                     QTest::keyClick(main, Qt::Key_Down);
                     QTest::qWait(50);
                 }
                 QTest::keyClick(main, Qt::Key_Return);
             }},
            {QStringLiteral("Return takes the row twelve below the pointer's"), [&] {
                 QTRY_VERIFY(!popover->property("opened").toBool());
                 const QString expected =
                     picker->property("options").value<QJSValue>().property(hovered + 12).property("id").toString();
                 QCOMPARE(picker->property("currentId").toString(), expected);
             }},
        });
    }

    // The Spoken language row names every language core knows, which the
    // picker and the lists of a service's languages show.
    void everyKnownLanguageHasAName()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Speecher.Wording
            QtObject { readonly property var ids: Schema.row("spokenLanguage").options.map(o => o.id) }
        )", QUrl());
        const std::unique_ptr<QObject> probe(component.create());
        QVERIFY2(probe, qPrintable(component.errorString()));
        QStringList named = probe->property("ids").value<QJSValue>().toVariant().toStringList();
        named.sort();
        QCOMPARE(named, knownSpokenLanguages());
    }

    // Closing a group hands a subpage's keyboard focus to its parent;
    // asking for a page whose group was closed by hand opens it again, even
    // when that page is already showing; and one group is open at a time.
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

        // One group open at a time: going to a page opens its group and
        // closes the others, and so does opening a group by hand, which
        // hands a closing subpage's focus to its parent too.
        QQuickItem *general = sidebarEntry(main, QStringLiteral("general"));
        QQuickItem *dictation = sidebarEntry(main, QStringLiteral("dictation"));
        QVERIFY(general && dictation);
        QMetaObject::invokeMethod(navigation, "go", Q_ARG(QVariant, QStringLiteral("accounts")));
        QVERIFY(general->property("open").toBool());
        QVERIFY(!refinement->property("open").toBool());
        QTest::qWait(kSettleMs);
        sidebarEntry(main, QStringLiteral("accounts"))->forceActiveFocus(Qt::TabFocusReason);
        QMetaObject::invokeMethod(dictation, "openRequested", Q_ARG(bool, true));
        QVERIFY(dictation->property("open").toBool());
        QVERIFY(!general->property("open").toBool());
        QVERIFY(general->hasActiveFocus());
        QMetaObject::invokeMethod(navigation, "go", Q_ARG(QVariant, QStringLiteral("home")));
        QVERIFY(!dictation->property("open").toBool());

        // A search that goes to another group from a subpage leaves focus on
        // the subpage's parent, not on the subpage it folded away.
        QMetaObject::invokeMethod(navigation, "go", Q_ARG(QVariant, QStringLiteral("profiles")));
        QTRY_VERIFY(refinement->property("open").toBool());
        QTest::qWait(kSettleMs);
        profiles->forceActiveFocus(Qt::TabFocusReason);
        auto *search = main->findChild<QObject *>(QStringLiteral("searchPalette"));
        QVERIFY(search);
        QMetaObject::invokeMethod(search, "open");
        const auto results = searchResults(search, QStringLiteral("accounts"));
        const auto accounts = std::find_if(results.cbegin(), results.cend(),
                                           [](const auto &result) { return result.first == QStringLiteral("accounts"); });
        QVERIFY(accounts != results.cend());
        QMetaObject::invokeMethod(search, "activate", Q_ARG(QVariant, int(accounts - results.cbegin())));
        QCOMPARE(navigation->property("current").toString(), QStringLiteral("accounts"));
        QVERIFY(!refinement->property("open").toBool());
        QVERIFY(!profiles->hasActiveFocus());
        QVERIFY(refinement->hasActiveFocus());
    }

    // Dictation's page says how each of its subpages is set now. The test
    // sets Popup & sounds' toggles, so that line is the same on every
    // platform; macOS and Windows bind a default shortcut where Linux has
    // none.
    void dictationSaysHowItsSubpagesAreSet()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("showLiveText"), false);
        settings.set(QStringLiteral("playSounds"), true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QObject *>(QStringLiteral("Speecher.App"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        QMetaObject::invokeMethod(navigation, "go", Q_ARG(QVariant, QStringLiteral("dictation")));
        const auto helpShown = [main](const QString &help) {
            QList<QQuickItem *> items{main->contentItem()};
            while (!items.isEmpty()) {
                QQuickItem *item = items.takeLast();
                items += item->childItems();
                if (item->isVisible() && item->property("help").toString() == help) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY(helpShown(QStringLiteral("Live text off, sounds on")));
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
             [session] { emit session->popupMessageRequested(QStringLiteral("Sent to the app"), PopupOutcome::Inserted); }},
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
