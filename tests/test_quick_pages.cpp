// The whole Quick UI as the daemon shows it, over the real Backend, in light
// and dark: every page of Main and a few searches, every setup step and every
// popup state load and draw without a QML warning, and the popup's level
// animation stays inside its bar.
#include "common/test_quick_windows.h"
#include "common/test_suites.h"

#include "app/AppFrontEnd.h"
#include "app/ApplicationController.h"
#include "app/TrayIcon.h"
#include "core/PasteRules.h"
#include "core/SettingsStore.h"
#include "core/Target.h"
#include "core/settings/SpokenLanguages.h"
#include "dictation/DictationSession.h"
#include "dictation/PopupPresentation.h"
#include "dictation/SelectionEditPresentation.h"
#include "frontend/quick/host/QuickFrontEnd.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/ScatterRibbon.h"

#include <QAccessible>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QPainter>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSGRendererInterface>
#include <QSignalSpy>
#include <QTextDocument>

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

// The SchemaRow for rowId in window, or null.
QQuickItem *schemaRow(QQuickWindow *window, const QString &rowId)
{
    QList<QQuickItem *> items{window->contentItem()};
    while (!items.isEmpty()) {
        QQuickItem *item = items.takeLast();
        items += item->childItems();
        if (item->property("rowId").toString() == rowId) {
            return item;
        }
    }
    return nullptr;
}

// The caption of the Section item sits in.
QString sectionCaption(QQuickItem *item)
{
    while (item && !item->property("caption").isValid()) {
        item = item->parentItem();
    }
    return item ? item->property("caption").toString() : QStringLiteral("(no section)");
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
        auto *navigation = engine->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        auto *schema = engine->singletonInstance<QObject *>(QStringLiteral("Speecher.Wording"), QStringLiteral("Schema"));
        auto *search = main->findChild<QObject *>(QStringLiteral("searchPalette"));
        QVERIFY(navigation && schema && search);
        const QVariantList pages = schema->property("pages").value<QJSValue>().toVariant().toList();
        QVERIFY(pages.size() > 10);

        QList<std::pair<QString, std::function<void()>>> steps;
        for (const QVariant &page : pages) {
            const QString id = page.toMap().value(QStringLiteral("id")).toString();
            steps.append({QStringLiteral("page ") + id, [navigation, id] {
                              navigation->go(id);
                              QCOMPARE(navigation->current(), id);
                          }});
        }
        // Rows moved to a subpage land there.
        for (const auto &[row, page] : {std::pair{QStringLiteral("themeControl"), QStringLiteral("appearance")},
                                        std::pair{QStringLiteral("microphoneAccess"), QStringLiteral("permissions")},
                                        std::pair{QStringLiteral("appRecognitionRules"), QStringLiteral("appRules")}}) {
            steps.append({QStringLiteral("row ") + row, [navigation, row, page] {
                              navigation->goToRow(page, row);
                              QCOMPARE(navigation->current(), page);
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

    // A fallback's picker offers "Same as main language" first and checks it
    // while the fallback has no language chosen, its button showing the
    // language the fallback listens for; choosing it drops the choice.
    void aFallbackLanguageCanFollowTheMainOneAgain()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("speechProvider"), QStringLiteral("codex"));
        settings.set(QStringLiteral("spokenLanguage"), QStringLiteral("de"));
        settings.set(QStringLiteral("speechFallbackList"), QStringList{QStringLiteral("claude")});
        settings.set(QStringLiteral("speechFallbackLanguages"), QVariantMap{{QStringLiteral("claude"), QStringLiteral("fr")}});
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("speech"));
        QQuickItem *picker = nullptr;
        QTRY_VERIFY((picker = findItem(main->contentItem(), QStringLiteral("fallbackLanguage_claude"))));
        const QVariantMap first =
            picker->property("options").value<QJSValue>().toVariant().toList().value(0).toMap();
        QCOMPARE(first.value(QStringLiteral("id")).toString(), QString());
        QCOMPARE(first.value(QStringLiteral("label")).toString(), QStringLiteral("Same as main language"));
        QCOMPARE(picker->property("currentId").toString(), QStringLiteral("fr"));
        QCOMPARE(picker->property("shownId").toString(), QStringLiteral("fr"));

        QVERIFY(QMetaObject::invokeMethod(picker, "chosen", Q_ARG(QString, QString())));
        QTRY_VERIFY(!controller.settings()->snapshot().speech.fallbackLanguages.contains(QStringLiteral("claude")));
        QTRY_COMPARE(picker->property("currentId").toString(), QString());
        QCOMPARE(picker->property("shownId").toString(), QStringLiteral("de"));
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
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
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
            {QStringLiteral("speech page"), [navigation] { navigation->go(QStringLiteral("speech")); }},
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
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("profiles"));
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

        navigation->go(QStringLiteral("profiles"));
        QVERIFY(refinement->property("open").toBool());

        QMetaObject::invokeMethod(refinement, "openRequested", Q_ARG(bool, false));
        navigation->goToRow(QStringLiteral("profiles"), QStringLiteral("defaultWritingProfile"));
        QVERIFY(refinement->property("open").toBool());
        QCOMPARE(navigation->current(), QStringLiteral("profiles"));

        // One group open at a time: going to a page opens its group and
        // closes the others, and so does opening a group by hand, which
        // hands a closing subpage's focus to its parent too.
        QQuickItem *general = sidebarEntry(main, QStringLiteral("general"));
        QQuickItem *dictation = sidebarEntry(main, QStringLiteral("dictation"));
        QVERIFY(general && dictation);
        navigation->go(QStringLiteral("accounts"));
        QVERIFY(general->property("open").toBool());
        QVERIFY(!refinement->property("open").toBool());
        QTest::qWait(kSettleMs);
        sidebarEntry(main, QStringLiteral("accounts"))->forceActiveFocus(Qt::TabFocusReason);
        QMetaObject::invokeMethod(dictation, "openRequested", Q_ARG(bool, true));
        QVERIFY(dictation->property("open").toBool());
        QVERIFY(!general->property("open").toBool());
        QVERIFY(general->hasActiveFocus());
        navigation->go(QStringLiteral("home"));
        QVERIFY(!dictation->property("open").toBool());

        // A search that goes to another group from a subpage leaves focus on
        // the subpage's parent, not on the subpage it folded away.
        navigation->go(QStringLiteral("profiles"));
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
        QCOMPARE(navigation->current(), QStringLiteral("accounts"));
        QVERIFY(!refinement->property("open").toBool());
        QVERIFY(!profiles->hasActiveFocus());
        QVERIFY(refinement->hasActiveFocus());
    }

    // Accounts shows the API key row only while the OpenAI auth mode is the
    // key saved in Speecher.
    void accountsShowsTheApiKeyRowForItsAuthMode()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("accounts"));
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(main->contentItem(), QStringLiteral("openAiApiKeyRow"))));
        QVERIFY(!row->property("shown").toBool());
        QuickSettings(&controller).set(QStringLiteral("openAiAuthMode"), QStringLiteral("settings"));
        QTRY_VERIFY(row->property("shown").toBool());
        QuickSettings(&controller).set(QStringLiteral("openAiAuthMode"), QStringLiteral("cliproxy"));
        QTRY_VERIFY(!row->property("shown").toBool());
    }

    // A search result for a row in a section folded away, such as the
    // Custom Endpoint's while Codex is chosen, opens the section and lands
    // on the row; keypad Enter picks a result as Return does.
    void searchOpensTheSectionItLandsIn()
    {
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("speechProvider"), QStringLiteral("codex"));
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        auto *search = main->findChild<QObject *>(QStringLiteral("searchPalette"));
        QVERIFY(navigation && search);

        QMetaObject::invokeMethod(search, "open");
        const auto results = searchResults(search, QStringLiteral("endpoint url"));
        const auto at = std::find_if(results.cbegin(), results.cend(),
                                     [](const auto &result) { return result.first == QStringLiteral("speechEndpointUrl"); });
        QVERIFY(at != results.cend());
        for (auto i = results.cbegin(); i != at; ++i) {
            QTest::keyClick(main, Qt::Key_Down);
        }
        QTest::keyClick(main, Qt::Key_Enter, Qt::KeypadModifier);

        QTRY_COMPARE(navigation->current(), QStringLiteral("speech"));
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = schemaRow(main, QStringLiteral("speechEndpointUrl"))));
        QTRY_VERIFY(row->isVisible() && row->height() > 0);
        // Landed, which clears the pending row, then scrolled into the window.
        QTRY_COMPARE(navigation->pendingRow(), QString());
        QTRY_VERIFY(row->mapToScene(QPointF(0, 0)).y() >= 0 && row->mapToScene(QPointF(0, row->height())).y() <= main->height());
        // Nothing uses the endpoint, so its section claims no role.
        QCOMPARE(sectionCaption(row), QString());
    }

    // A search for a fallback refinement provider's row, such as Claude's
    // model while ChatGPT refines, shows that provider's settings.
    void searchShowsTheProviderSettingsItLandsIn()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("refinementProvider"), QStringLiteral("openai"));
        settings.set(QStringLiteral("refinementFallbacks"), QVariantList{QStringLiteral("anthropic")});
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->goToRow(QStringLiteral("refinement"), QStringLiteral("anthropicModel"));

        QQuickItem *tabs = nullptr;
        QTRY_VERIFY((tabs = findItem(main->contentItem(), QStringLiteral("providerTabs"))));
        QTRY_COMPARE(tabs->property("currentId").toString(), QStringLiteral("anthropic"));
        QQuickItem *row = schemaRow(main, QStringLiteral("anthropicModel"));
        QVERIFY(row);
        QTRY_VERIFY(row->isVisible() && row->height() > 0);
        QTRY_COMPARE(navigation->pendingRow(), QString());
    }

    // A search for a row whose section is hidden on purpose, such as the
    // Global Shortcut where the desktop gives none, leaves it hidden.
    void searchLeavesHiddenSectionsHidden()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->goToRow(QStringLiteral("shortcuts"), QStringLiteral("globalShortcut"));
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = schemaRow(main, QStringLiteral("globalShortcut"))));
        QQuickItem *page = row;
        while (page && !page->property("noShortcuts").isValid()) {
            page = page->parentItem();
        }
        QVERIFY(page);
        if (!page->property("noShortcuts").toBool()) {
            QSKIP("This desktop gives global shortcuts, so the section shows anyway");
        }
        // Past the landing's wait and any Reveal's growth.
        QTRY_COMPARE(navigation->pendingRow(), QString());
        QTest::qWait(kSettleMs);
        QVERIFY(!row->isVisible());
    }

    // A service a Writing Profile chooses for itself shows its settings on
    // Speech to text, as the chosen one and its fallbacks do.
    void speechShowsTheSettingsOfAServiceAProfileChooses()
    {
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("speechProvider"), QStringLiteral("codex"));
        controller.settings()->edit([](AppSettings &draft) {
            for (WritingProfileSettings &profile : draft.refinement.writingProfiles) {
                if (profile.profile == QLatin1String("email")) {
                    profile.speechProvider = QStringLiteral("endpoint");
                }
            }
            return true;
        });
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("speech"));
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = schemaRow(main, QStringLiteral("speechEndpointUrl"))));
        QTRY_VERIFY(row->isVisible() && row->height() > 0);
        QCOMPARE(sectionCaption(row), QStringLiteral("Used by your Email profile."));
    }

    // A proof keeps the text's own spacing, and closes up only the gap a
    // word left out by its mode leaves.
    void proofTextKeepsItsSpacing()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import Speecher.Controls\nDiffText {}\n", QUrl());
        const std::unique_ptr<QObject> diff(component.create());
        QVERIFY2(diff, qPrintable(component.errorString()));
        const auto segment = [](const char *kind, const char *text) {
            return QVariantMap{{QStringLiteral("kind"), QLatin1String(kind)}, {QStringLiteral("text"), QString::fromUtf8(text)}};
        };
        const QVariantList edit{segment("same", "Well"), segment("same", " "), segment("removed", "um"),
                                segment("same", " "), segment("same", "so"), segment("same", " "),
                                segment("removed", "anyway"), segment("same", ","), segment("same", " "),
                                segment("added", "fine"), segment("same", "  done")};
        const auto shown = [&diff, &edit](const char *mode) {
            QVariant html;
            QMetaObject::invokeMethod(diff.get(), "html", Q_RETURN_ARG(QVariant, html), Q_ARG(QVariant, edit),
                                      Q_ARG(QVariant, QLatin1String(mode)));
            QTextDocument document;
            document.setHtml(html.toString());
            return document.toPlainText();
        };
        QCOMPARE(shown("marked"), QStringLiteral("Well um so anyway, fine  done"));
        QCOMPARE(shown("spoken"), QStringLiteral("Well um so anyway, done"));
        QCOMPARE(shown("result"), QStringLiteral("Well so, fine  done"));
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
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("dictation"));
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

    // Clicking another profile, or pressing it with an assistive technology,
    // saves the field being edited to the profile it was edited for, before
    // the editor shows the other one.
    void choosingAProfileSavesTheEditInProgress()
    {
        QAccessible::setActive(true);
        const auto inactive = qScopeGuard([] { QAccessible::setActive(false); });
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("profiles"));
        QQuickItem *instructions = nullptr;
        QTRY_VERIFY((instructions = findItem(main->contentItem(), QStringLiteral("profileInstructions"))));
        // The page stack drops presses while the page is still coming in,
        // which takes longer than any fixed wait on a slow runner.
        QQuickItem *stack = instructions;
        while (stack && !stack->inherits("QQuickStackView")) {
            stack = stack->parentItem();
        }
        QVERIFY(stack);
        QTRY_VERIFY(!stack->property("busy").toBool());
        const auto profileRow = [main](const QString &id) -> QQuickItem * {
            QList<QQuickItem *> items{main->contentItem()};
            while (!items.isEmpty()) {
                QQuickItem *item = items.takeLast();
                items += item->childItems();
                if (item->property("clickable").toBool() && item->property("key").toString() == id) {
                    return item;
                }
            }
            return nullptr;
        };
        const auto instructionsOf = [&controller](const QString &id) {
            for (const WritingProfileSettings &profile : controller.settings()->snapshot().refinement.writingProfiles) {
                if (profile.profile == id) {
                    return profile.instructions;
                }
            }
            return QString();
        };
        QQuickItem *email = profileRow(QStringLiteral("email"));
        QVERIFY(email);
        QVERIFY(profileRow(QStringLiteral("work"))->property("selected").toBool());

        instructions->forceActiveFocus(Qt::MouseFocusReason);
        for (const char *key = "Keep it short."; *key; ++key) {
            QTest::keyClick(main, *key);
        }
        QTest::mouseClick(main, Qt::LeftButton, Qt::NoModifier,
                          email->mapToScene(QPointF(email->width() / 2, email->height() / 2)).toPoint());

        QTRY_VERIFY(email->property("selected").toBool());
        QCOMPARE(instructionsOf(QStringLiteral("work")), QStringLiteral("Keep it short."));
        QCOMPARE(instructionsOf(QStringLiteral("email")), QString());

        instructions->forceActiveFocus(Qt::MouseFocusReason);
        for (const char *key = "Sign off."; *key; ++key) {
            QTest::keyClick(main, *key);
        }
        QQuickItem *work = profileRow(QStringLiteral("work"));
        QAccessible::queryAccessibleInterface(work)->actionInterface()->doAction(QAccessibleActionInterface::pressAction());

        QTRY_VERIFY(work->property("selected").toBool());
        QCOMPARE(instructionsOf(QStringLiteral("email")), QStringLiteral("Sign off."));
        QCOMPARE(instructionsOf(QStringLiteral("work")), QStringLiteral("Keep it short."));
    }

    // At the window's narrowest, an app match's Writing Profile choice ends
    // before its Edit and Delete buttons, so a click on it can't delete the
    // match.
    void appMatchChoicesClearTheirButtons()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            draft.appRecognitionRules = {{.match = QStringLiteral("org.kde.konsole"), .category = AppCategory::Terminal}};
            return true;
        });
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        main->resize(main->minimumWidth(), main->height());
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("appRules"));

        QQuickItem *profile = nullptr;
        QTRY_VERIFY((profile = findItem(main->contentItem(), QStringLiteral("matchProfile"))));
        QQuickItem *row = profile;
        while (row && !row->property("actions").isValid()) {
            row = row->parentItem();
        }
        QVERIFY(row);
        QQuickItem *edit = nullptr;
        QList<QQuickItem *> items{row};
        while (!items.isEmpty() && !edit) {
            QQuickItem *item = items.takeLast();
            items += item->childItems();
            if (item->property("iconName").toString() == QLatin1String("pencil")) {
                edit = item;
            }
        }
        QVERIFY(edit);
        QTest::qWait(kSettleMs);
        const qreal profileEnd = profile->mapToScene(QPointF(profile->width(), 0)).x();
        const qreal buttonsStart = edit->mapToScene(QPointF(0, 0)).x();
        QVERIFY2(profileEnd <= buttonsStart, qPrintable(QStringLiteral("%1 > %2").arg(profileEnd).arg(buttonsStart)));
    }

    // Undo after deleting an app's paste rule puts it back, even once the
    // page it was deleted on is gone.
    void pasteRuleUndoOutlivesItsPage()
    {
        ApplicationController controller(true);
        QuickSettings(&controller)
            .set(QStringLiteral("applicationPasteRules"),
                 QVariantList{QVariantMap{{QStringLiteral("app"), QStringLiteral("org.kde.konsole")},
                                          {QStringLiteral("method"), QStringLiteral("standard_paste")},
                                          {QStringLiteral("enabled"), true}}});
        const auto appRules = [&controller] {
            QStringList apps;
            for (const PasteRule &rule : controller.settings()->snapshot().output.pasteRules) {
                if (rule.scope == PasteRuleScope::Application) {
                    apps.append(rule.match);
                }
            }
            return apps;
        };
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        QQmlEngine *engine = qmlEngine(main);
        auto *navigation = engine->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        auto *toaster = engine->singletonInstance<QObject *>(QStringLiteral("Speecher.Controls"), QStringLiteral("Toaster"));
        QVERIFY(navigation && toaster);
        QSignalSpy posted(toaster, SIGNAL(posted(QVariant)));
        navigation->go(QStringLiteral("appRules"));
        QPointer<QQuickItem> section;
        QTRY_VERIFY((section = findItem(main->contentItem(), QStringLiteral("appPasteRules"))));

        QMetaObject::invokeMethod(section, "remove", Q_ARG(QVariant, 0));
        QCOMPARE(posted.count(), 1);
        QVERIFY(appRules().isEmpty());
        navigation->go(QStringLiteral("home"));
        QTRY_VERIFY(!section);

        const QJSValue undone = posted.first().first().value<QJSValue>().property(QStringLiteral("action")).call();
        QVERIFY2(!undone.isError(), qPrintable(undone.toString()));
        QCOMPARE(appRules(), QStringList{QStringLiteral("org.kde.konsole")});
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

    // Finishing the assistant records setup as done, so it does not run again.
    void finishingSetupRecordsItDone()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showSetupAssistant(SetupAssistantPage::All);
        QQuickWindow *setup = openWindow("SetupAssistant");
        QVERIFY(setup);
        QVERIFY(!controller.settings()->dictationSnapshot().setupCompleted);
        QVERIFY(QMetaObject::invokeMethod(setup, "finish"));
        QVERIFY(controller.settings()->dictationSnapshot().setupCompleted);
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
        emit session->stateChanged(DictationState::Starting);
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
            emit session->stateChanged(DictationState::Reviewing);
            emit session->popupSelectionEditReviewRequested(edit);
        };
        runSteps({
            {QStringLiteral("starting"), [] {}},
            {QStringLiteral("renewing"), [session] { emit session->popupOAuthRefreshRequested(); }},
            {QStringLiteral("listening"),
             [session] {
                 emit session->stateChanged(DictationState::Listening);
                 emit session->audioLevelChanged(0.6f);
                 emit session->previewDisplayChanged(QStringLiteral("Quick update on the launch"));
             }},
            {QStringLiteral("paused"), [session] { emit session->stateChanged(DictationState::Paused); }},
            {QStringLiteral("transcribing"), [session] { emit session->stateChanged(DictationState::Stopping); }},
            {QStringLiteral("refining"),
             [session] {
                 emit session->stateChanged(DictationState::Refining);
                 emit session->popupRefinementPreviewChanged(QStringLiteral("Quick update on the launch."));
             }},
            {QStringLiteral("delivering"), [session] { emit session->stateChanged(DictationState::Delivering); }},
            {QStringLiteral("delivered"),
             [session] { emit session->popupMessageRequested(QStringLiteral("Sent to the app"), PopupOutcome::Inserted); }},
            {QStringLiteral("review"), [review] { review(false); }},
            {QStringLiteral("following"), [review] { review(true); }},
            {QStringLiteral("error"),
             [session] {
                 emit session->stateChanged(DictationState::Error);
                 emit session->popupErrorRequested(QStringLiteral("Couldn't reach Claude • Check your connection."),
                                                   PopupErrorAction{});
             }},
        });
    }

    // Listening, the popup shows the scatter ribbon, or in Boring mode the
    // level bars and the record dot, and switches as the setting changes.
    // The bars show only where the ribbon would: listening and paused, not
    // while starting, renewing a sign-in or transcribing.
    void boringModeShowsLevelBars()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        DictationSession *session = controller.session();
        emit session->stateChanged(DictationState::Listening);
        emit session->popupShowRequested(1);
        QQuickWindow *popup = openWindow("PopupWindow");
        QVERIFY(popup);
        QVERIFY(QTest::qWaitForWindowExposed(popup));
        auto *settings = qmlEngine(popup)->singletonInstance<QuickSettings *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Settings"));
        QVERIFY(settings);
        QQuickItem *ribbon = findItem(popup->contentItem(), QStringLiteral("ribbon"));
        QQuickItem *bars = findItem(popup->contentItem(), QStringLiteral("levelBars"));
        QQuickItem *orb = findItem(popup->contentItem(), QStringLiteral("orb"));
        QVERIFY(ribbon && bars && orb);
        const auto showsRibbon = [&] {
            return ribbon->opacity() == 1 && !bars->isVisible() && orb->property("phase").toString().isEmpty();
        };
        const auto showsBars = [&] {
            return ribbon->opacity() == 0 && bars->opacity() == 1 && orb->property("phase").toString() == QLatin1String("listening");
        };

        QTRY_VERIFY(showsRibbon());
        settings->set(QStringLiteral("boringMode"), true);
        QTRY_VERIFY(showsBars());

        const struct {
            const char *phase;
            std::function<void()> enter;
            bool barsShown;
        } phases[] = {
            {"paused", [session] { emit session->stateChanged(DictationState::Paused); }, true},
            {"renewing", [session] { emit session->popupOAuthRefreshRequested(); }, false},
            {"transcribing", [session] { emit session->stateChanged(DictationState::Stopping); }, false},
            {"starting", [session] { emit session->stateChanged(DictationState::Starting); }, false},
            {"listening", [session] { emit session->stateChanged(DictationState::Listening); }, true},
        };
        for (const auto &phase : phases) {
            qInfo() << "phase:" << phase.phase;
            phase.enter();
            QTRY_COMPARE(bars->isVisible(), phase.barsShown);
            QCOMPARE(ribbon->isVisible(), false);
        }

        // Live words widen the capsule from 280 to 460; the bars are counted
        // once for the new width, not on every frame as it grows.
        QQuickItem *capsule = findItem(popup->contentItem(), QStringLiteral("capsule"));
        QVERIFY(capsule);
        QSignalSpy recounted(bars, SIGNAL(barsChanged()));
        emit session->previewDisplayChanged(QStringLiteral("Quick update on the launch"));
        QTRY_COMPARE(capsule->width(), 460.0);
        QTest::qWait(kSettleMs);
        QCOMPARE(capsule->width(), 460.0);
        QCOMPARE(recounted.count(), 1);

        settings->set(QStringLiteral("boringMode"), false);
        QTRY_VERIFY(showsRibbon());
    }

    // The popup's ribbon keeps every point finite and inside its strip, the
    // bar, through a loud voice, a pause, the capsule widening from 280 to
    // 460 for live words and reduced motion.
    void ribbonStaysInsideTheBar()
    {
        RibbonFigure figure;
        // The strip is 160 px wide in the 280 capsule and 340 in the 460.
        const struct {
            qreal seconds;
            qreal width;
            bool listening;
            qreal movement;
        } phases[] = {{3, 160, true, 1}, {1, 160, false, 1}, {2, 340, true, 1}, {1, 340, true, 0}};
        qreal loosest = 0;
        for (const auto &phase : phases) {
            const QRectF strip(0, 0, phase.width, 44);
            for (int frame = 0; frame < phase.seconds * 60; ++frame) {
                // A level every 50 ms, a word starting every 100 ms.
                if (frame % 3 == 0) {
                    figure.hear(frame % 6 == 0 ? 0.95 : 0.05, phase.listening);
                }
                figure.step(1.0 / 60, strip.size(), phase.listening, phase.movement);
                for (const RibbonFigure::Point &point : figure.points()) {
                    QVERIFY2(strip.contains(point.at), qPrintable(QStringLiteral("%1, %2").arg(point.at.x()).arg(point.at.y())));
                    loosest = std::max(loosest, point.looseness);
                }
            }
        }
        // The voice did scatter points, so the bounds were tested.
        QVERIFY(loosest > 0.5);
    }

    // The ribbon's loop keeps its size in the 280 capsule, about 145 px, as
    // the capsule widens to 460: a second loop unfurls beside it rather than
    // either stretching.
    void ribbonLoopsKeepTheirSize()
    {
        RibbonFigure figure;
        // Each loop's width, from the points that show; below 0 for a loop that doesn't.
        const auto widths = [&figure] {
            qreal low[2] = {1e9, 1e9}, high[2] = {-1e9, -1e9};
            for (const RibbonFigure::Point &point : figure.points()) {
                if (point.shown > 0) {
                    low[point.second] = std::min(low[point.second], point.home.x());
                    high[point.second] = std::max(high[point.second], point.home.x());
                }
            }
            return std::pair{high[0] - low[0], high[1] - low[1]};
        };
        // The strip widens from 160 to 332 px over half a second, as the
        // capsule does once the clock reads 10:00 and takes a digit's room.
        for (int frame = 0; frame < 120; ++frame) {
            figure.step(1.0 / 60, {160 + 172 * std::min(1.0, frame / 30.0), 44}, true, 1);
            const auto [first, second] = widths();
            QVERIFY2(first < 150 && second < 150, qPrintable(QStringLiteral("%1, %2").arg(first).arg(second)));
        }
        const auto [first, second] = widths();
        QVERIFY2(std::abs(first - 145) < 5 && std::abs(second - 145) < 5,
                 qPrintable(QStringLiteral("%1, %2").arg(first).arg(second)));
    }

    // Shown again while the voice holds one level, the ribbon still hears it
    // and warms toward its accent, drawn here as white from black.
    void reshownRibbonHearsTheLevel()
    {
        // An item shows again only under a parent.
        QQuickItem bar;
        ScatterRibbon ribbon;
        ribbon.setParentItem(&bar);
        ribbon.setSize({160, 44});
        ribbon.setProperty("color", QColor(Qt::black));
        ribbon.setProperty("freedColor", QColor(Qt::white));
        ribbon.setListening(true);
        ribbon.setLevel(0.9);
        ribbon.setVisible(false);
        ribbon.setVisible(true);
        for (int frame = 0; frame < 60; ++frame) {
            ribbon.advance(1.0 / 60);
        }
        QImage image(160, 44, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        ribbon.paint(&painter);
        painter.end();
        int warm = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = QColor::fromRgba(qUnpremultiply(image.pixel(x, y)));
                warm += pixel.alpha() > 64 && pixel.lightness() > 128;
            }
        }
        QVERIFY(warm > 0);
    }

    // Paused after a loud voice, the ribbon goes on until it rests: further
    // frames would draw it no stronger or fainter than where it stopped.
    void pausedRibbonStopsAtRest()
    {
        ScatterRibbon ribbon;
        ribbon.setSize({160, 44});
        ribbon.setListening(true);
        ribbon.setLevel(1);
        for (int frame = 0; frame < 60; ++frame) {
            ribbon.advance(1.0 / 60);
        }
        ribbon.setListening(false);
        while (ribbon.moving()) {
            ribbon.advance(1.0 / 60);
        }
        const auto drawn = [&ribbon] {
            QImage image(160, 44, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            ribbon.paint(&painter);
            return image;
        };
        // Its strength: the sum of its alpha.
        const auto strength = [](const QImage &image) {
            qint64 sum = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    sum += qAlpha(image.pixel(x, y));
                }
            }
            return sum;
        };
        const qint64 stopped = strength(drawn());
        for (int frame = 0; frame < 120; ++frame) {
            ribbon.advance(1.0 / 60);
        }
        QVERIFY2(std::abs(strength(drawn()) - stopped) < stopped / 50, qPrintable(QStringLiteral("%1 then %2").arg(stopped).arg(strength(drawn()))));
    }

    // Once paused and still, the ribbon draws no more frames, yet takes a
    // new theme's colours.
    void stillRibbonTakesNewColours()
    {
        ScatterRibbon ribbon;
        ribbon.setSize({160, 44});
        ribbon.setProperty("pausedColor", QColor(Qt::black));
        while (ribbon.moving()) {
            ribbon.advance(1.0 / 60);
        }
        const auto drawn = [&ribbon] {
            QImage image(160, 44, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            ribbon.paint(&painter);
            return image;
        };
        // Its strength: the sum of its alpha.
        const auto strength = [](const QImage &image) {
            qint64 sum = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    sum += qAlpha(image.pixel(x, y));
                }
            }
            return sum;
        };
        const QImage before = drawn();
        ribbon.setProperty("pausedColor", QColor(Qt::white));
        QVERIFY(drawn() != before);
        // Back in the first colours it is as strong as it was.
        ribbon.setProperty("pausedColor", QColor(Qt::black));
        QVERIFY(std::abs(strength(drawn()) - strength(before)) < strength(before) / 20);
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
