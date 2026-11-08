// The whole Quick UI as the daemon shows it, over the real Backend, in light
// and dark: every page of Main and a few searches, every setup step and every
// popup state load and draw without a QML warning, and the popup's level
// animation stays inside its bar.
#include "common/service_order_rows.h"
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
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
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
#include <QTemporaryDir>
#include <QTextDocument>

#include <algorithm>
#include <cmath>
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

// Where UX review screenshots go, or empty for none.
QString captureDir()
{
    return qEnvironmentVariable("SPEECHER_UI_CAPTURE_DIR");
}

// Saves window as <theme>-NN-<step>.png in captureDir(), first making it
// tall enough that its page shows whole.
void capture(QQuickWindow *window, int index, const QString &step)
{
    QQuickItem *scroll = nullptr;
    QList<QQuickItem *> items{window->contentItem()};
    while (!items.isEmpty() && !scroll) {
        QQuickItem *item = items.takeLast();
        items += item->childItems();
        if (item->objectName() == QLatin1String("pageScroll") && item->isVisible()) {
            scroll = item;
        }
    }
    QVERIFY(scroll);
    const auto overflow = [scroll] { return scroll->property("contentHeight").toReal() - scroll->height(); };
    // Waits until the window and the page hold their heights: a window
    // manager can apply a resize late, and a page can grow a little as it
    // lays out at the new height, easing there (Grow).
    const auto settle = [&overflow] {
        QTest::qWait(kSettleMs);
        for (int wait = 0; wait < 20; ++wait) {
            const qreal before = overflow();
            QTest::qWait(50);
            if (overflow() == before) {
                return;
            }
        }
    };
    for (int fit = 0; fit < 5 && (fit == 0 || overflow() > 0); ++fit) {
        window->resize(1100, std::max(820, int(std::ceil(window->height() + overflow()))));
        settle();
    }
    // Under a pixel is the window's whole-pixel height rounding a
    // fractional page height.
    QVERIFY2(overflow() < 1, qPrintable(QStringLiteral("%1 is cut off by %2 px").arg(step).arg(overflow())));
    const QString slug = step.toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9]+")), QStringLiteral("-"));
    const QString file = QStringLiteral("%1-%2-%3.png").arg(QLatin1String(QTest::currentDataTag())).arg(index, 2, 10, QLatin1Char('0')).arg(slug);
    QVERIFY(QDir().mkpath(captureDir()));
    QVERIFY(window->grabWindow().save(QDir(captureDir()).filePath(file)));
}

// Runs each step and lets the window settle after it, so a warning a step
// causes fails the test there; the log names the step. A step that fails
// stops the rest, which may rely on what it found. With captureDir() set,
// saves window after each step.
void runSteps(const QList<std::pair<QString, std::function<void()>>> &steps, QQuickWindow *window = nullptr)
{
    for (int i = 0; i < steps.size(); ++i) {
        const auto &[name, step] = steps.at(i);
        qInfo().noquote() << "step:" << name;
        step();
        if (QTest::currentTestFailed()) {
            return;
        }
        QTest::qWait(kSettleMs);
        if (window && !captureDir().isEmpty()) {
            capture(window, i, name);
            if (QTest::currentTestFailed()) {
                return;
            }
        }
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
QQuickItem *schemaRow(QQuickItem *root, const QString &rowId)
{
    QList<QQuickItem *> items{root};
    while (!items.isEmpty()) {
        QQuickItem *item = items.takeLast();
        items += item->childItems();
        if (item->property("rowId").toString() == rowId) {
            return item;
        }
    }
    return nullptr;
}

QQuickItem *schemaRow(QQuickWindow *window, const QString &rowId)
{
    return schemaRow(window->contentItem(), rowId);
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
        // Screenshots draw with the UI's own fonts, which only the app
        // carries.
        if (!captureDir().isEmpty()) {
            QDirIterator fonts(QStringLiteral(SPEECHER_SOURCE_DIR "/src/frontend/quick/assets"), {QStringLiteral("*.ttf")}, QDir::Files,
                               QDirIterator::Subdirectories);
            while (fonts.hasNext()) {
                QVERIFY(QFontDatabase::addApplicationFont(fonts.next()) >= 0);
            }
        }
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
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("themeControl"), theme);
        // Orders whose rows show every kind of choice: a named model, a
        // server's, a later service's language, thinking and speed.
        settings.set(QStringLiteral("speechServices"),
                     test::serviceOrder({QStringLiteral("claude"), QStringLiteral("codex"), QStringLiteral("endpoint")}));
        settings.set(QStringLiteral("refinementServices"),
                     test::serviceOrder({QStringLiteral("openai"), QStringLiteral("anthropic"), QStringLiteral("local")}));
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
                                        std::pair{QStringLiteral("microphoneAccess"), QStringLiteral("microphone")},
                                        std::pair{QStringLiteral("appRecognitionRules"), QStringLiteral("appRecognition")},
                                        std::pair{QStringLiteral("speechEndpointUrl"), QStringLiteral("services")},
                                        std::pair{QStringLiteral("pressPasteKeys"), QStringLiteral("pasting")}}) {
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
                          QVERIFY(searchResults(search, QStringLiteral("keybinding"))
                                      .contains(std::pair{QStringLiteral("startCommand"), QStringLiteral("Dictation, How shortcuts reach Speecher")}));
                          // Services is a page of its own, with no path.
                          QVERIFY(searchResults(search, QStringLiteral("services"))
                                      .contains(std::pair{QStringLiteral("services"), QString()}));
                      }});
        steps.append({QStringLiteral("close search"), [search] { QMetaObject::invokeMethod(search, "close"); }});
        // For review, App recognition with what folds away open, and a
        // speech service's Fix sheet over Speech to text.
        if (!captureDir().isEmpty()) {
            steps.append({QStringLiteral("speech fix sheet"), [navigation, main] {
                              navigation->go(QStringLiteral("speech"));
                              QQuickItem *fix = nullptr;
                              QTRY_VERIFY((fix = findItem(main->contentItem(), QStringLiteral("fix_endpoint"))));
                              QMetaObject::invokeMethod(fix, "clicked");
                          }});
            steps.append({QStringLiteral("close fix sheet"), [main] {
                              QObject *sheet = main->findChild<QObject *>(QStringLiteral("fixSheet"));
                              if (sheet) QMetaObject::invokeMethod(sheet, "close");
                          }});
            steps.append({QStringLiteral("app recognition with built-in matches open"), [navigation, main] {
                              navigation->go(QStringLiteral("appRecognition"));
                              QQuickItem *builtIns = nullptr;
                              QTRY_VERIFY((builtIns = findItem(main->contentItem(), QStringLiteral("builtInMatches"))));
                              builtIns->setProperty("open", true);
                          }});
        }
        runSteps(steps, main);
    }

    // A later service's picker offers "Same as Spoken language" first and
    // checks it while the service has no language chosen, its button
    // showing the language it listens for; choosing it drops the choice.
    void aLaterServiceLanguageCanFollowTheSpokenOneAgain()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("spokenLanguage"), QStringLiteral("de"));
        settings.set(QStringLiteral("speechServices"),
                     QVariantList{QVariantMap{{QStringLiteral("service"), QStringLiteral("codex")}},
                                  QVariantMap{{QStringLiteral("service"), QStringLiteral("claude")},
                                              {QStringLiteral("language"), QStringLiteral("fr")}}});
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
        QTRY_VERIFY((picker = findItem(main->contentItem(), QStringLiteral("serviceLanguage_claude"))));
        const QVariantMap first =
            picker->property("options").value<QJSValue>().toVariant().toList().value(0).toMap();
        QCOMPARE(first.value(QStringLiteral("id")).toString(), QString());
        QCOMPARE(first.value(QStringLiteral("label")).toString(), QStringLiteral("Same as Spoken language"));
        QCOMPARE(picker->property("currentId").toString(), QStringLiteral("fr"));
        QCOMPARE(picker->property("shownId").toString(), QStringLiteral("fr"));

        QVERIFY(QMetaObject::invokeMethod(picker, "chosen", Q_ARG(QString, QString())));
        QTRY_VERIFY(controller.settings()->snapshot().speech.services.choice(QStringLiteral("claude")).language.isEmpty());
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
        QuickSettings(&controller).set(QStringLiteral("speechServices"),
                                       test::serviceOrder({QStringLiteral("claude"), QStringLiteral("codex")}));
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        const auto language = [&controller] { return controller.settings()->snapshot().speech.language; };
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
            {QStringLiteral("follow ChatGPT's count of languages"), [&] {
                 QTRY_VERIFY(!popover->property("opened").toBool());
                 QCOMPARE(language(), QStringLiteral("fo"));
                 QCOMPARE(picker->property("currentId").toString(), QStringLiteral("fo"));

                 QQuickItem *codexLanguages = findItem(main->contentItem(), QStringLiteral("languages_codex"));
                 serviceLanguages = main->findChild<QObject *>(QStringLiteral("serviceLanguages"));
                 QVERIFY(codexLanguages);
                 QVERIFY(serviceLanguages);
                 QCOMPARE(codexLanguages->property("link").toString(), QStringLiteral("67 languages"));
                 QVERIFY(QMetaObject::invokeMethod(codexLanguages, "activated"));
             }},
            {QStringLiteral("only ChatGPT's languages"), [&] {
                 QTRY_VERIFY(serviceLanguages->property("opened").toBool());
                 auto *codexRows = serviceLanguages->findChild<QObject *>(QStringLiteral("languageRows"));
                 QVERIFY(codexRows);
                 QCOMPARE(codexRows->property("count").toInt(), 67);
                 // Following it chooses nothing.
                 QCOMPARE(controller.settings()->snapshot().speech.services.order,
                          (QStringList{QStringLiteral("claude"), QStringLiteral("codex")}));
                 QTest::keyClick(main, Qt::Key_Escape);
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
        navigation->go(QStringLiteral("appearance"));
        QVERIFY(general->property("open").toBool());
        QVERIFY(!refinement->property("open").toBool());
        QTest::qWait(kSettleMs);
        sidebarEntry(main, QStringLiteral("appearance"))->forceActiveFocus(Qt::TabFocusReason);
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
        const auto results = searchResults(search, QStringLiteral("services"));
        const auto accounts = std::find_if(results.cbegin(), results.cend(),
                                           [](const auto &result) { return result.first == QStringLiteral("services"); });
        QVERIFY(accounts != results.cend());
        QMetaObject::invokeMethod(search, "activate", Q_ARG(QVariant, int(accounts - results.cbegin())));
        QCOMPARE(navigation->current(), QStringLiteral("services"));
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
        navigation->go(QStringLiteral("services"));
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(main->contentItem(), QStringLiteral("openAiApiKeyRow"))));
        QVERIFY(!row->property("shown").toBool());
        QuickSettings(&controller).set(QStringLiteral("openAiAuthMode"), QStringLiteral("settings"));
        QTRY_VERIFY(row->property("shown").toBool());
        QuickSettings(&controller).set(QStringLiteral("openAiAuthMode"), QStringLiteral("cliproxy"));
        QTRY_VERIFY(!row->property("shown").toBool());
    }

    // CLI Proxy API is a way to sign in to ChatGPT or Claude: its account,
    // directory and server show in the card that signs in through it.
    void cliProxyRowsShowInTheCardThatSignsInThroughIt()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("services"));
        QQuickItem *card = nullptr;
        QTRY_VERIFY((card = findItem(main->contentItem(), QStringLiteral("chatgptCard"))));
        QQuickItem *directory = schemaRow(card, QStringLiteral("cliproxyOauthDir"));
        QVERIFY(directory);
        QVERIFY(!directory->property("shown").toBool());

        QuickSettings(&controller).set(QStringLiteral("openAiAuthMode"), QStringLiteral("cliproxy"));
        QTRY_VERIFY(directory->property("shown").toBool());
        QVERIFY(schemaRow(card, QStringLiteral("cliproxyBaseUrl"))->property("shown").toBool());
        QQuickItem *claude = findItem(main->contentItem(), QStringLiteral("claudeCard"));
        QVERIFY(claude);
        QVERIFY(!schemaRow(claude, QStringLiteral("cliproxyOauthDir"))->property("shown").toBool());
    }

    // A link to another page, such as Open Local models under This
    // computer, offers the way back above the page it opens, until the
    // person goes anywhere else.
    void aLinkOffersTheWayBack()
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
        navigation->go(QStringLiteral("services"));
        QQuickItem *openModels = nullptr;
        QTRY_VERIFY((openModels = findItem(main->contentItem(), QStringLiteral("openLocalModels"))));
        QMetaObject::invokeMethod(openModels, "clicked");
        QCOMPARE(navigation->current(), QStringLiteral("models"));
        QCOMPARE(navigation->returnTo(), QStringLiteral("services"));
        const auto backLink = [main] {
            QQuickItem *link = findItem(main->contentItem(), QStringLiteral("backLink"));
            return link && link->isVisible() && link->property("text") == QStringLiteral("Back to Services") ? link : nullptr;
        };
        QTRY_VERIFY(backLink());
        QMetaObject::invokeMethod(backLink(), "activated");
        QCOMPARE(navigation->current(), QStringLiteral("services"));
        QCOMPARE(navigation->returnTo(), QString());

        // Going anywhere else forgets it, and a search jump is not a link.
        navigation->follow(QStringLiteral("models"));
        QCOMPARE(navigation->returnTo(), QStringLiteral("services"));
        navigation->goToRow(QStringLiteral("refinement"), QStringLiteral("refinementServices"));
        QCOMPARE(navigation->returnTo(), QString());
        navigation->follow(QStringLiteral("services"));
        QCOMPARE(navigation->returnTo(), QStringLiteral("refinement"));
        navigation->go(QStringLiteral("home"));
        QCOMPARE(navigation->returnTo(), QString());
    }

    // A speech service in the order that can't run offers Fix, which opens
    // its Services card in a sheet over the page; Open Services in the sheet
    // follows to the whole page, from setup too, whose way back is to setup.
    void aFixSheetOpensTheServicesCard()
    {
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("speechServices"), test::serviceOrder({QStringLiteral("endpoint")}));
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("speech"));
        QQuickItem *fix = nullptr;
        // No server URL is set, which Services fixes.
        QTRY_VERIFY((fix = findItem(main->contentItem(), QStringLiteral("fix_endpoint"))) && fix->isVisible());
        QMetaObject::invokeMethod(fix, "clicked");
        QObject *sheet = nullptr;
        QTRY_VERIFY((sheet = main->findChild<QObject *>(QStringLiteral("fixSheet"))) && sheet->property("opened").toBool());
        QCOMPARE(sheet->property("card").toString(), QStringLiteral("servers"));
        QTRY_VERIFY(schemaRow(main->contentItem(), QStringLiteral("speechEndpointUrl")));
        QCOMPARE(navigation->current(), QStringLiteral("speech"));

        QQuickItem *openServices = nullptr;
        QTRY_VERIFY((openServices = findItem(main->contentItem(), QStringLiteral("openServices"))));
        QMetaObject::invokeMethod(openServices, "activated");
        QCOMPARE(navigation->current(), QStringLiteral("services"));
        QCOMPARE(navigation->returnTo(), QStringLiteral("speech"));
        QTRY_VERIFY(!sheet->property("visible").toBool());
    }

    // Open Services in setup opens Main on Services, whose way back is to
    // setup; once setup closes there is nothing to go back to.
    void setupsServicesLinkReturnsToSetup()
    {
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("speechServices"), test::serviceOrder({QStringLiteral("endpoint")}));
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showSetupAssistant(SetupAssistantPage::All);
        QQuickWindow *setup = openWindow("SetupAssistant");
        QVERIFY(setup);
        QVERIFY(QTest::qWaitForWindowExposed(setup));
        QVERIFY(!openWindow("Main"));
        const QJSValue steps = setup->property("steps").value<QJSValue>();
        for (int i = 0; i < steps.property(QStringLiteral("length")).toInt(); ++i) {
            if (steps.property(i).property(QStringLiteral("id")).toString() == QLatin1String("transcription")) {
                setup->setProperty("step", i);
            }
        }
        QQuickItem *fix = nullptr;
        QTRY_VERIFY((fix = findItem(setup->contentItem(), QStringLiteral("fixService"))));
        QMetaObject::invokeMethod(fix, "clicked");
        QQuickItem *openServices = nullptr;
        QTRY_VERIFY((openServices = findItem(setup->contentItem(), QStringLiteral("openServices"))));
        QMetaObject::invokeMethod(openServices, "activated");
        QQuickWindow *main = nullptr;
        QTRY_VERIFY((main = openWindow("Main")));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        QCOMPARE(navigation->current(), QStringLiteral("services"));
        QCOMPARE(navigation->returnTo(), QStringLiteral("setup"));
        QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("backLink"))
                    && findItem(main->contentItem(), QStringLiteral("backLink"))->property("text") == QStringLiteral("Back to setup"));
        navigation->goBack();
        QCOMPARE(navigation->current(), QStringLiteral("services"));
        QCOMPARE(navigation->returnTo(), QString());
        QVERIFY(setup->isVisible());

        navigation->follow(QStringLiteral("services"), QStringLiteral("anthropicAuthMode"), QStringLiteral("setup"));
        QCOMPARE(navigation->returnTo(), QStringLiteral("setup"));
        setup->close();
        QTRY_COMPARE(navigation->returnTo(), QString());
    }

    // While ChatGPT's Sign in with stays on Automatic, its row says which
    // source Automatic's check found.
    void signInNamesWhatAutomaticFound()
    {
        // No Codex sign-in, so Automatic finds the environment's key.
        QTemporaryDir codexHome;
        QVERIFY(codexHome.isValid());
        const QByteArray oldCodexPath = qgetenv("SPEECHER_TEST_CODEX_AUTH_PATH");
        const QByteArray oldKey = qgetenv("OPENAI_API_KEY");
        const auto restore = qScopeGuard([&] {
            oldCodexPath.isNull() ? qunsetenv("SPEECHER_TEST_CODEX_AUTH_PATH") : qputenv("SPEECHER_TEST_CODEX_AUTH_PATH", oldCodexPath);
            oldKey.isNull() ? qunsetenv("OPENAI_API_KEY") : qputenv("OPENAI_API_KEY", oldKey);
        });
        qputenv("SPEECHER_TEST_CODEX_AUTH_PATH", QFile::encodeName(codexHome.filePath(QStringLiteral("auth.json"))));
        qputenv("OPENAI_API_KEY", "sk-test");

        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("openAiAuthMode"), QStringLiteral("auto"));
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("services"));
        QQuickItem *card = nullptr;
        QTRY_VERIFY((card = findItem(main->contentItem(), QStringLiteral("chatgptCard"))));
        QQuickItem *mode = schemaRow(card, QStringLiteral("openAiAuthMode"));
        QVERIFY(mode);
        QTRY_VERIFY(mode->property("note").toString().startsWith(QStringLiteral("Using API key from the environment.")));

        // A source chosen by hand needs no line.
        settings.set(QStringLiteral("openAiAuthMode"), QStringLiteral("env"));
        QTRY_VERIFY(!mode->property("note").toString().startsWith(QStringLiteral("Using")));
    }

    // A search result for a row on another page, such as the speech
    // server's URL among Your servers, lands on the row; keypad Enter picks
    // a result as Return does.
    void searchLandsOnARowOfAnotherPage()
    {
        ApplicationController controller(true);
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
        const auto results = searchResults(search, QStringLiteral("server url whisper"));
        const auto at = std::find_if(results.cbegin(), results.cend(),
                                     [](const auto &result) { return result.first == QStringLiteral("speechEndpointUrl"); });
        QVERIFY(at != results.cend());
        for (auto i = results.cbegin(); i != at; ++i) {
            QTest::keyClick(main, Qt::Key_Down);
        }
        QTest::keyClick(main, Qt::Key_Enter, Qt::KeypadModifier);

        QTRY_COMPARE(navigation->current(), QStringLiteral("services"));
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = schemaRow(main, QStringLiteral("speechEndpointUrl"))));
        QTRY_VERIFY(row->isVisible() && row->height() > 0);
        // Landed, which clears the pending row, then scrolled into the window.
        QTRY_COMPARE(navigation->pendingRow(), QString());
        QTRY_VERIFY(row->mapToScene(QPointF(0, 0)).y() >= 0 && row->mapToScene(QPointF(0, row->height())).y() <= main->height());
    }

    // A search for a refinement service's thinking lands on Refinement's
    // order, where each service shows its own.
    void searchLandsOnTheServiceOrder()
    {
        ApplicationController controller(true);
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
        const auto results = searchResults(search, QStringLiteral("thinking"));
        QVERIFY(std::any_of(results.cbegin(), results.cend(),
                            [](const auto &result) { return result.first == QStringLiteral("refinementServices"); }));
        QMetaObject::invokeMethod(search, "close");

        navigation->goToRow(QStringLiteral("refinement"), QStringLiteral("refinementServices"));
        QQuickItem *list = nullptr;
        QTRY_VERIFY((list = findItem(main->contentItem(), QStringLiteral("refinementServiceOrder"))));
        QTRY_VERIFY(list->isVisible() && list->height() > 0);
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
        navigation->goToRow(QStringLiteral("dictation"), QStringLiteral("globalShortcut"));
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

    // A search for a row in a page's Advanced group, a timing row on
    // Microphone or the system prompt on Refinement, opens the group, which
    // is closed otherwise, and lands on the row.
    void searchOpensAnAdvancedGroup_data()
    {
        QTest::addColumn<QString>("pageId");
        QTest::addColumn<QString>("rowId");
        QTest::newRow("microphone") << QStringLiteral("microphone") << QStringLiteral("preRollMs");
        QTest::newRow("refinement") << QStringLiteral("refinement") << QStringLiteral("customSystemPromptEnabled");
    }
    void searchOpensAnAdvancedGroup()
    {
        QFETCH(QString, pageId);
        QFETCH(QString, rowId);
        ApplicationController controller(true);
        // Refinement shows the prompt while a service refines.
        QuickSettings(&controller).set(QStringLiteral("refinementServices"), test::serviceOrder({QStringLiteral("openai")}));
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(pageId);
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = schemaRow(main, rowId)));
        QTest::qWait(kSettleMs);
        QVERIFY(!row->isVisible());

        navigation->goToRow(pageId, rowId);
        QTRY_VERIFY(row->isVisible() && row->height() > 0);
        QTRY_COMPARE(navigation->pendingRow(), QString());
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
    // platform.
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

    // Under the dictation shortcut's keys, that clicking them changes it;
    // with no keys, or while it listens for new ones, the field itself says
    // what to do, and what pressing them does is the cards' to say. The test
    // gives the field its keys, as a desktop may pick them itself.
    void dictationShortcutSaysItCanBeChanged()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("dictation"));
        QQuickItem *caption = nullptr;
        QQuickItem *recorder = nullptr;
        QTRY_VERIFY((caption = findItem(main->contentItem(), QStringLiteral("shortcutCaption"))));
        QVERIFY((recorder = findItem(main->contentItem(), QStringLiteral("dictationShortcut"))));
        recorder->setProperty("keys", QStringList{QStringLiteral("Ctrl"), QStringLiteral("Alt"), QStringLiteral("D")});
        QTRY_COMPARE(caption->property("text").toString(), QStringLiteral("Click to change"));
        recorder->setProperty("listening", true);
        QTRY_COMPARE(caption->property("text").toString(), QString());
        recorder->setProperty("listening", false);
        recorder->setProperty("keys", QStringList{});
        QTRY_COMPARE(caption->property("text").toString(), QString());
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
    // Writing profiles and App recognition count app matches in the same
    // words: how many, and how many of them are yours.
    void appMatchCountsReadAlike()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QObject *library = qmlEngine(main)->singletonInstance<QObject *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Library"));
        QVERIFY(library);
        const auto count = [library](int total, int own) {
            QVariant text;
            QMetaObject::invokeMethod(library, "matchCount", Q_RETURN_ARG(QVariant, text), Q_ARG(QVariant, total), Q_ARG(QVariant, own));
            return text.toString();
        };
        QCOMPARE(count(22, 0), QStringLiteral("22 app matches, all built in"));
        QCOMPARE(count(3, 1), QStringLiteral("3 app matches, 1 of yours"));
        QCOMPARE(count(1, 1), QStringLiteral("1 app match, all yours"));
        // With none of your own, every count says so.
        QVERIFY(library->property("recognitionRulesText").toString().endsWith(QLatin1String(" app matches, all built in")));
        QVariant work;
        QMetaObject::invokeMethod(library, "profileApps", Q_RETURN_ARG(QVariant, work), Q_ARG(QVariant, QStringLiteral("work")));
        QVariant workCount;
        QMetaObject::invokeMethod(library, "profileMatches", Q_RETURN_ARG(QVariant, workCount), Q_ARG(QVariant, work));
        QVERIFY(workCount.toString().endsWith(QLatin1String(" app matches, all built in")));
    }

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
        navigation->go(QStringLiteral("appRecognition"));

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

    // At the window's narrowest, each exception's name and result end
    // before its choice instead of running under it.
    void exceptionsKeepTheirWordsClearOfTheirChoices()
    {
        ApplicationController controller(true);
        QuickSettings(&controller)
            .set(QStringLiteral("applicationPasteRules"),
                 QVariantList{QVariantMap{{QStringLiteral("app"), QStringLiteral("org.example.AnApplicationWithAVeryLongDesktopId")},
                                          {QStringLiteral("method"), QStringLiteral("direct_insert")},
                                          {QStringLiteral("enabled"), true}}});
        QuickSettings(&controller).set(QStringLiteral("categoryPasteRule_ai_coding"), QStringLiteral("clipboard_only"));
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        main->resize(main->minimumWidth(), main->height());
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("pasting"));
        QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("exception_type:ai_coding")));
        QTest::qWait(kSettleMs);

        int checked = 0;
        QList<QQuickItem *> items{main->contentItem()};
        while (!items.isEmpty()) {
            QQuickItem *row = items.takeLast();
            items += row->childItems();
            if (!row->objectName().startsWith(QLatin1String("exception_"))) {
                continue;
            }
            // The row's words, and the left end of its choice, a Select. The
            // switch and the delete button come after it.
            QList<QQuickItem *> words;
            qreal choiceStart = -1;
            QList<QQuickItem *> parts{row};
            while (!parts.isEmpty()) {
                QQuickItem *part = parts.takeLast();
                if (QByteArray(part->metaObject()->className()).startsWith("Select_")) {
                    choiceStart = part->mapToScene(QPointF(0, 0)).x();
                    continue;
                }
                parts += part->childItems();
                if (part->isVisible() && part->property("contentWidth").isValid() && !part->property("text").toString().isEmpty()) {
                    words.append(part);
                }
            }
            QVERIFY2(choiceStart >= 0 && !words.isEmpty(), qPrintable(row->objectName()));
            words.removeIf([choiceStart](QQuickItem *text) { return text->mapToScene(QPointF(0, 0)).x() >= choiceStart; });
            QVERIFY2(!words.isEmpty(), qPrintable(row->objectName()));
            for (QQuickItem *text : words) {
                const qreal end = text->mapToScene(QPointF(std::min(text->property("contentWidth").toReal(), text->width()), 0)).x();
                QVERIFY2(end <= choiceStart, qPrintable(QStringLiteral("%1: \"%2\" ends at %3, its choice starts at %4")
                                                            .arg(row->objectName(), text->property("text").toString())
                                                            .arg(end)
                                                            .arg(choiceStart)));
            }
            ++checked;
        }
        QCOMPARE(checked, terminalKeysDiffer ? 3 : 2);
        // What the long ID's exception does has a line of its own, whole.
        QQuickItem *app = findItem(main->contentItem(), QStringLiteral("exception_app:org.example.AnApplicationWithAVeryLongDesktopId"));
        QVERIFY(app);
        QQuickItem *result = findItem(app, QStringLiteral("exceptionResult"));
        QVERIFY(result);
        QVERIFY(!result->property("text").toString().isEmpty());
        QVERIFY(!result->property("truncated").toBool());
    }

    // A saved exception for one app is found by its ID or its rule, and the
    // result lands on its row.
    void searchFindsASavedExceptionAndLandsOnIt()
    {
        ApplicationController controller(true);
        QuickSettings(&controller)
            .set(QStringLiteral("applicationPasteRules"),
                 QVariantList{QVariantMap{{QStringLiteral("app"), QStringLiteral("org.example.FooEditor")},
                                          {QStringLiteral("method"), QStringLiteral("clipboard_only")},
                                          {QStringLiteral("enabled"), true}}});
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        auto *search = main->findChild<QObject *>(QStringLiteral("searchPalette"));
        QVERIFY(navigation && search);
        const QString rowId = QStringLiteral("exception_app:org.example.FooEditor");
        const auto finds = [&search, &rowId](const QString &query) {
            const auto results = searchResults(search, query);
            return std::any_of(results.cbegin(), results.cend(), [&rowId](const auto &result) { return result.first == rowId; });
        };
        QMetaObject::invokeMethod(search, "open");
        QVERIFY(finds(QStringLiteral("fooeditor")));
        QVERIFY(finds(QStringLiteral("clipboard only exception")));
        QVERIFY(!finds(QStringLiteral("terminal keys")));

        const auto results = searchResults(search, QStringLiteral("org.example.fooeditor"));
        QCOMPARE(results.first(), std::make_pair(rowId, QStringLiteral("Pasting, Exceptions")));
        QTest::keyClick(main, Qt::Key_Return);
        QTRY_COMPARE(navigation->current(), QStringLiteral("pasting"));
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(main->contentItem(), rowId)));
        QTRY_COMPARE(navigation->pendingRow(), QString());
        QVERIFY(row->property("selected").toBool());
        QTRY_VERIFY(row->mapToScene(QPointF(0, 0)).y() >= 0 && row->mapToScene(QPointF(0, row->height())).y() <= main->height());
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
        navigation->go(QStringLiteral("pasting"));
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
        const QJSValue info = setup->property("steps").value<QJSValue>();
        const int count = info.property(QStringLiteral("length")).toInt();
        QVERIFY(count > 3);

        QList<std::pair<QString, std::function<void()>>> steps;
        for (int step = 0; step < count; ++step) {
            const QString id = info.property(step).property(QStringLiteral("id")).toString();
            steps.append({QStringLiteral("setup step %1 %2").arg(step).arg(id), [setup, step] { setup->setProperty("step", step); }});
        }
        runSteps(steps, setup);
    }

    // Refinement passes with None or a service that works; one that can't
    // clean up yet holds the step and says why, so setup can't call it done.
    void setupRefinementPassesOnlyWhenCleanupWorks()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("refinementServices"), test::serviceOrder({QStringLiteral("endpoint")}));
        settings.set(QStringLiteral("refinementEndpointUrl"), QString());
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showSetupAssistant(SetupAssistantPage::All);
        QQuickWindow *setup = openWindow("SetupAssistant");
        QVERIFY(setup);
        const auto refinement = [setup] {
            const QJSValue steps = setup->property("steps").value<QJSValue>();
            for (int i = 0; i < steps.property(QStringLiteral("length")).toInt(); ++i) {
                if (steps.property(i).property(QStringLiteral("id")).toString() == QLatin1String("refinement")) {
                    return steps.property(i);
                }
            }
            return QJSValue();
        };
        QVERIFY(!refinement().property(QStringLiteral("done")).toBool());
        QVERIFY(refinement().property(QStringLiteral("verdict")).toString() != QLatin1String("Custom Endpoint"));

        settings.set(QStringLiteral("refinementEnabled"), false);
        QTRY_VERIFY(refinement().property(QStringLiteral("done")).toBool());
        QCOMPARE(refinement().property(QStringLiteral("verdict")).toString(), QStringLiteral("None"));
    }

    // Finish is there on every step's way; while a step dictation needs is
    // open, finishing saves the choices but leaves setup to run again, and
    // the footer says what is missing. No step's check holds back the next.
    void finishingSetupRecordsItDoneOnlyOnceNothingNeededIsOpen()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showSetupAssistant(SetupAssistantPage::All);
        QQuickWindow *setup = openWindow("SetupAssistant");
        QVERIFY(setup);
        auto *choices = setup->findChild<QObject *>(QStringLiteral("setupChoices"));
        QVERIFY(choices);
        // Here no microphone has heard anything.
        QVERIFY(!choices->property("complete").toBool());
        const int last = setup->property("steps").value<QJSValue>().property(QStringLiteral("length")).toInt() - 1;
        for (int step = 1; step <= last; ++step) {
            QVERIFY(QMetaObject::invokeMethod(setup, "go", Q_ARG(QVariant, step)));
            QCOMPARE(setup->property("step").toInt(), step);
        }
        const auto shows = [setup](const QString &text) {
            QList<QQuickItem *> items{setup->contentItem()};
            while (!items.isEmpty()) {
                QQuickItem *item = items.takeLast();
                items += item->childItems();
                if (item->property("text").toString() == text) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY(shows(QStringLiteral("Finish for now")));
        choices->setProperty("keep", false);
        QVERIFY(QMetaObject::invokeMethod(setup, "finish"));
        QVERIFY(!controller.settings()->dictationSnapshot().setupCompleted);
        QVERIFY(!controller.settings()->dictationSnapshot().output.steps.keep);
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
        // once for the new width, not on every frame as it grows, and on no
        // frame show past their own strip, over the clock and buttons.
        QQuickItem *capsule = findItem(popup->contentItem(), QStringLiteral("capsule"));
        QVERIFY(capsule);
        // How far the bars show past their strip's right end.
        const auto overrun = [bars] {
            const qreal shown = bars->clip() ? bars->width() : bars->childrenRect().right();
            return shown - bars->width();
        };
        QSignalSpy recounted(bars, SIGNAL(barsChanged()));
        emit session->previewDisplayChanged(QStringLiteral("Quick update on the launch"));
        QElapsedTimer widening;
        widening.start();
        qreal worst = 0;
        while (widening.elapsed() < 1000) {
            worst = std::max(worst, overrun());
            QTest::qWait(5);
        }
        QCOMPARE(capsule->width(), 460.0);
        QCOMPARE(recounted.count(), 1);
        QCOMPARE(worst, 0.0);

        // A follow-up listens under the edit it asks more of, in the wider
        // capsule of a review; the bars fill its strip, listening and paused.
        const auto fillsTheStrip = [bars] {
            const int expected = std::max(8, int(std::floor(bars->width() / 5.5)));
            return bars->isVisible() && bars->property("bars").toInt() == expected;
        };
        SelectionEditReview edit = selectionEditReview(
            QStringLiteral("Can you send me the deck when you get a chance?"),
            QStringLiteral("Can you send me the slides when you can?"), {QStringLiteral("make it shorter")},
            reviewKeysFor(true, true, QString(), QStringLiteral("Ctrl+Space")));
        edit.following = true;
        emit session->popupSelectionEditReviewRequested(edit);
        emit session->previewDisplayChanged(QString());
        QTRY_VERIFY(capsule->width() > 460);
        QTest::qWait(kSettleMs);
        QTRY_VERIFY(fillsTheStrip());
        emit session->stateChanged(DictationState::Paused);
        QTest::qWait(kSettleMs);
        QTRY_VERIFY(fillsTheStrip());

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
