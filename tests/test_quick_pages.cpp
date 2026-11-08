// The whole Quick UI as the daemon shows it, over the real Backend, in light
// and dark: every page of Main and a few searches, every setup step and every
// popup state load and draw without a QML warning, and the popup's level
// animation stays inside its bar.
#include "common/service_order_rows.h"
#include "common/test_doubles.h"
#include "common/test_quick_windows.h"
#include "common/test_suites.h"
#include "common/apps_fixtures.h"
#include "common/vocabulary_entries.h"

#include "app/AppFrontEnd.h"
#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "app/TrayIcon.h"
#include "core/SettingsStore.h"
#include "core/SeenApps.h"
#include "core/Target.h"
#include "core/settings/SpokenLanguages.h"
#include "core/history/HistoryStore.h"
#include "dictation/DictationSession.h"
#include "dictation/PopupPresentation.h"
#include "dictation/SelectionEditPresentation.h"
#include "frontend/quick/host/QuickApps.h"
#include "frontend/quick/host/QuickFrontEnd.h"
#include "frontend/quick/host/QuickRecording.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/QuickSystem.h"
#include "frontend/quick/host/ScatterRibbon.h"
#include "providers/ProviderRegistry.h"
#include "recording/RecordingSession.h"

#include <QAccessible>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QSet>
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

// The help of the first row under root whose help holds part, or empty.
QString rowHelp(QQuickItem *root, const QString &part)
{
    QList<QQuickItem *> items{root};
    while (!items.isEmpty()) {
        QQuickItem *item = items.takeLast();
        items += item->childItems();
        const QString help = item->property("help").toString();
        if (help.contains(part)) {
            return help;
        }
    }
    return {};
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

// This platform, but a Recording's microphone and system audio are fakes
// the test drives, so the Recordings page and the pill can be seen in each
// state of a real recording.
class RecordingPlatform final : public PlatformComposition {
public:
    QString ipcListenName() const override { return m_platform->ipcListenName(); }
    QStringList ipcConnectCandidates() const override { return m_platform->ipcConnectCandidates(); }
    QString detachedExecutablePath() const override { return m_platform->detachedExecutablePath(); }
    QString outputSummary() const override { return m_platform->outputSummary(); }
    QList<AudioInputDeviceInfo> availableAudioInputDevices() const override { return m_platform->availableAudioInputDevices(); }
    AudioInput *createAudioInput(SettingsStore *settings, QObject *parent) const override
    {
        return m_platform->createAudioInput(settings, parent);
    }
    AudioInput *createRecordingAudioInput(SettingsStore *, QObject *parent) const override
    {
        return microphone = new test::FakeAudioInput(parent);
    }
    bool recordsSystemAudio() const override { return true; }
    AudioInput *createSystemAudioInput(QObject *parent) const override { return systemAudio = new test::FakeAudioInput(parent); }
    MediaController *createMediaController(QObject *parent) const override { return m_platform->createMediaController(parent); }
    TargetProvider *createTargetProvider(QObject *parent) const override { return m_platform->createTargetProvider(parent); }
    ScreenshotContextProvider *createScreenshotContextProvider(QObject *parent) const override
    {
        return m_platform->createScreenshotContextProvider(parent);
    }
    TextDeliveryAdapter *createTextDelivery(TargetProvider *targetProvider, QObject *parent) const override
    {
        return m_platform->createTextDelivery(targetProvider, parent);
    }
    PopupPositioner *createPopupPositioner(QObject *parent) const override { return m_platform->createPopupPositioner(parent); }
    GlobalShortcutBinder *createGlobalShortcutBinder(GlobalShortcutRole role, SettingsStore *settings,
                                                     QObject *parent) const override
    {
        return m_platform->createGlobalShortcutBinder(role, settings, parent);
    }
    AccessibilityState accessibilityState() const override { return m_platform->accessibilityState(); }
    bool requestAccessibility(QString *error) const override { return m_platform->requestAccessibility(error); }
    bool enableAccessibilityPermanently(QString *error) const override
    {
        return m_platform->enableAccessibilityPermanently(error);
    }

    mutable QPointer<test::FakeAudioInput> microphone;
    mutable QPointer<test::FakeAudioInput> systemAudio;

private:
    std::shared_ptr<const PlatformComposition> m_platform = platformComposition();
};

// A dictation History keeps the text of, finished now.
HistoryEntry keptDictation(const QString &text)
{
    HistoryEntry entry;
    entry.stats = {QDateTime::currentDateTime(), 6000, int(text.split(u' ').size()), QStringLiteral("Kate"), WritingProfile::Other};
    entry.kept = HistoryKeep::Text;
    entry.text = text;
    return entry;
}

// Deletes every Recording in History, which clearing leaves and whose file
// outlives the test.
void forgetRecordings(ApplicationController &controller)
{
    HistoryQuery recordings;
    recordings.kinds = {HistoryKind::Recording};
    recordings.limit = 1000;
    for (const HistoryHit &hit : controller.history()->search(recordings)) {
        controller.history()->remove(hit.entry.id);
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
        // History, Recordings and Home show real entries: dictations, a
        // file and a recording that missed some of the call, in a History
        // in memory, on a pinned today.
        qputenv("SPEECHER_HISTORY_SEED", QFINDTESTDATA("data/history/seed.json").toUtf8());
        qputenv("SPEECHER_INSIGHTS_TODAY", "2026-11-08");
        const auto unseed = qScopeGuard([] {
            qunsetenv("SPEECHER_HISTORY_SEED");
            qunsetenv("SPEECHER_INSIGHTS_TODAY");
        });
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("themeControl"), theme);
        // Orders whose rows show every kind of choice: a named model, a
        // server's, a later service's language, thinking and speed.
        settings.set(QStringLiteral("speechServices"),
                     test::serviceOrder({QStringLiteral("claude"), QStringLiteral("codex"), QStringLiteral("endpoint")}));
        settings.set(QStringLiteral("refinementServices"),
                     test::serviceOrder({QStringLiteral("openai"), QStringLiteral("anthropic"), QStringLiteral("local")}));
        // Apps, Pasting and Writing profiles show what an App and an App
        // Type set, and Add app the apps dictated into.
        controller.settings()->edit([](AppSettings &draft) {
            AppEntry gmail = test::textApp(QStringLiteral("Gmail"), std::nullopt, WritingProfile::Email);
            gmail.matches = {{AppMatchKind::WindowTitle, QStringLiteral("Gmail")}};
            gmail.pasting.steps = DeliverySteps{.insert = false, .press = false, .keep = true};
            draft.apps.apps = {test::idApp(QStringLiteral("org.kde.kate"), WritingProfile::Personal,
                                           {DeliverySteps{.insert = true, .press = true, .keep = false}, PasteKeyCombo::Terminal}),
                               gmail};
            draft.apps.apps.first().name = QStringLiteral("Kate");
            // Privacy & data lists an app kept out of History.
            draft.apps.apps.first().history = HistoryKeep::Stats;
            draft.apps.types[1].writingProfile = WritingProfile::Work;
            // Writing profiles shows Work with services of its own, a
            // refinement server's model typed.
            for (WritingProfileSettings &profile : draft.refinement.writingProfiles) {
                if (profile.profile == WritingProfile::Work) {
                    profile.speechFirst = ServiceChoice{QStringLiteral("codex")};
                    profile.refinementFirst = ServiceChoice{QStringLiteral("endpoint"), QStringLiteral("qwen3:8b")};
                }
            }
            // Vocabulary shows each kind: a key term with a context, one
            // limited to a profile, a snippet over several lines, one
            // limited to an App, and a Learned Correction with its evidence.
            VocabularyEntry kubernetes{QStringLiteral("Kubernetes")};
            kubernetes.context = QStringLiteral("The container platform");
            kubernetes.frequency = 12;
            VocabularyEntry lucia{QStringLiteral("Lúcia")};
            lucia.profiles = {WritingProfile::Personal};
            lucia.keyTerm = false;
            VocabularyEntry signature = test::replacementEntry(QStringLiteral("sign off"),
                                                               QStringLiteral("Best,\nEnzo\nSent {date}"));
            signature.profiles = {WritingProfile::Email};
            VocabularyEntry inKate = test::replacementEntry(QStringLiteral("todo"), QStringLiteral("// TODO(enzo):"));
            inKate.id = QStringLiteral("snippet-kate");
            inKate.apps = {draft.apps.apps.first().id};
            VocabularyEntry learned = test::learnedEntry(QStringLiteral("cute"), QStringLiteral("Qt"),
                                                         QStringLiteral("org.kde.kate"), QStringLiteral("learned-qt"));
            learned.evidenceCount = 3;
            draft.vocabulary = {kubernetes, lucia, signature, inKate, learned};
            return true;
        });
        DictationRecord seen{QDateTime::currentDateTime(), 1000, 3, QStringLiteral("Slack")};
        seen.applicationId = QStringLiteral("slack");
        controller.seenApps()->record(seen);
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
                                        std::pair{QStringLiteral("otherAppsProfile"), QStringLiteral("apps")},
                                        std::pair{QStringLiteral("appsPasteDifferently"), QStringLiteral("pasting")},
                                        std::pair{QStringLiteral("speechEndpointUrl"), QStringLiteral("services")},
                                        std::pair{QStringLiteral("pressPasteKeys"), QStringLiteral("pasting")},
                                        std::pair{QStringLiteral("recordingFolder"), QStringLiteral("recordings")},
                                        std::pair{QStringLiteral("recordingAutoStop"), QStringLiteral("recordings")},
                                        std::pair{QStringLiteral("historyKeeps"), QStringLiteral("privacy")}}) {
            steps.append({QStringLiteral("row ") + row, [navigation, row, page] {
                              navigation->goToRow(page, row);
                              QCOMPARE(navigation->current(), page);
                          }});
        }
        for (const QString query : {QStringLiteral("shortcut"), QStringLiteral("api key"), QStringLiteral("paste"), QStringLiteral("refinement"),
                                    QStringLiteral("app type"), QStringLiteral("quarterly deck"), QStringLiteral("zzz no match")}) {
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
                          const auto child = searchResults(search, QStringLiteral("spelling"));
                          QVERIFY(std::any_of(child.cbegin(), child.cend(), [](const auto &result) {
                              return result.second == QStringLiteral("Vocabulary, Your vocabulary");
                          }));
                          // Vocabulary is a page of its own, and its entries
                          // are found by what they write.
                          QVERIFY(searchResults(search, QStringLiteral("vocabulary"))
                                      .contains(std::pair{QStringLiteral("vocabulary"), QString()}));
                          QVERIFY(searchResults(search, QStringLiteral("TODO(enzo)"))
                                      .contains(std::pair{QStringLiteral("vocab:snippet-kate"),
                                                          QStringLiteral("Vocabulary, Your vocabulary")}));
                          QVERIFY(searchResults(search, QStringLiteral("keybinding"))
                                      .contains(std::pair{QStringLiteral("startCommand"), QStringLiteral("Dictation, How shortcuts reach Speecher")}));
                          // Recordings is a page of its own, and its settings
                          // are found by what they do.
                          QVERIFY(searchResults(search, QStringLiteral("recordings"))
                                      .contains(std::pair{QStringLiteral("recordings"), QString()}));
                          QVERIFY(searchResults(search, QStringLiteral("silence"))
                                      .contains(std::pair{QStringLiteral("recordingAutoStop"),
                                                          QStringLiteral("Recordings, Recording settings")}));
                          // Privacy & data is General's subpage, and holds
                          // what Speecher keeps.
                          QVERIFY(searchResults(search, QStringLiteral("privacy"))
                                      .contains(std::pair{QStringLiteral("privacy"), QStringLiteral("General › Privacy & data")}));
                          QVERIFY(searchResults(search, QStringLiteral("what speecher keeps"))
                                      .contains(std::pair{QStringLiteral("historyKeeps"),
                                                          QStringLiteral("General › Privacy & data, History")}));
                          // Services is a page of its own, with no path.
                          QVERIFY(searchResults(search, QStringLiteral("services"))
                                      .contains(std::pair{QStringLiteral("services"), QString()}));
                      }});
        steps.append({QStringLiteral("close search"), [search] { QMetaObject::invokeMethod(search, "close"); }});
        // History with a dictation open, then a file's entry, then the
        // recording's reader.
        const auto openRow = [main](qint64 id) {
            QQuickItem *row = nullptr;
            QTRY_VERIFY((row = findItem(main->contentItem(), QStringLiteral("historyRow_%1").arg(id))));
            QMetaObject::invokeMethod(row, "clicked");
        };
        const auto newest = [&controller](HistoryKind kind) {
            HistoryQuery query;
            query.kinds = {kind};
            query.limit = 1;
            return controller.history()->search(query).first().entry.id;
        };
        steps.append({QStringLiteral("history with an entry open"), [navigation, &openRow, &newest] {
                          navigation->go(QStringLiteral("history"));
                          openRow(newest(HistoryKind::Dictation));
                      }});
        // A file opens in the reader, as a recording does.
        steps.append({QStringLiteral("history with a file open"), [&openRow, &newest, main] {
                          const qint64 file = newest(HistoryKind::File);
                          openRow(file);
                          QQuickItem *reader = nullptr;
                          QTRY_VERIFY((reader = findItem(main->contentItem(), QStringLiteral("entryReader"))));
                          QTRY_COMPARE(reader->property("entryId").toLongLong(), file);
                          QVERIFY(findItem(reader, QStringLiteral("transcribeAgain")));
                      }});
        steps.append({QStringLiteral("history with a recording open"), [navigation, main, &newest] {
                          navigation->goToRow(QStringLiteral("history"), QStringLiteral("recording:%1").arg(newest(HistoryKind::Recording)));
                          QQuickItem *ending = nullptr;
                          QTRY_VERIFY((ending = findItem(main->contentItem(), QStringLiteral("recordingEnding"))));
                          QTRY_VERIFY(ending->isVisible());
                      }});
        // For review, a speech service's Fix sheet over Speech to text, then
        // Apps with an app's editor open, then with Add app's picker open.
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
            steps.append({QStringLiteral("apps with an editor open"), [navigation, main] {
                              navigation->goToRow(QStringLiteral("apps"), QStringLiteral("appentry:app:org.kde.kate"));
                              QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("appentry:app:org.kde.kate")));
                          }});
            steps.append({QStringLiteral("apps with the picker open"), [navigation] {
                              navigation->goToRow(QStringLiteral("apps"), QStringLiteral("addApp"));
                          }});
            steps.append({QStringLiteral("vocabulary with an entry open"), [navigation, main] {
                              navigation->goToRow(QStringLiteral("vocabulary"), QStringLiteral("vocab:learned-qt"));
                              QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("vocab:learned-qt")));
                          }});
            steps.append({QStringLiteral("vocabulary limited to Kate"), [navigation] {
                              navigation->goToRow(QStringLiteral("vocabulary"), QStringLiteral("vocabularyFor:app/app:org.kde.kate"));
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
        navigation->goToRow(QStringLiteral("profiles"), QStringLiteral("additionalInstructions"));
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

    // CLI Proxy API is a way to sign in to ChatGPT or Claude: the card that
    // signs in through it chooses its account, and the directory and server
    // both share show once, in a section of their own.
    void cliProxyRowsShowOnceForBothServices()
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
        QQuickItem *shared = findItem(main->contentItem(), QStringLiteral("cliproxySection"));
        QVERIFY(shared);
        QQuickItem *account = schemaRow(card, QStringLiteral("openAiCliproxyAccount"));
        QVERIFY(account);
        QVERIFY(!account->property("shown").toBool());
        QVERIFY(!shared->parentItem()->parentItem()->property("open").toBool());

        QuickSettings(&controller).set(QStringLiteral("openAiAuthMode"), QStringLiteral("cliproxy"));
        QTRY_VERIFY(account->property("shown").toBool());
        QVERIFY(shared->parentItem()->parentItem()->property("open").toBool());
        QVERIFY(schemaRow(shared, QStringLiteral("cliproxyOauthDir")));
        QVERIFY(schemaRow(shared, QStringLiteral("cliproxyBaseUrl")));
        // Neither card has a copy of them.
        QQuickItem *claude = findItem(main->contentItem(), QStringLiteral("claudeCard"));
        QVERIFY(claude);
        for (QQuickItem *signIn : {card, claude}) {
            QVERIFY(!schemaRow(signIn, QStringLiteral("cliproxyOauthDir")));
            QVERIFY(!schemaRow(signIn, QStringLiteral("cliproxyBaseUrl")));
        }
    }

    // This computer is configured in one place, Services' subpage: Services
    // keeps a row saying how it stands, and the subpage holds the cleanup
    // runner beside the speech models.
    void thisComputerIsOnePage()
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
        QQuickItem *summary = nullptr;
        QTRY_VERIFY((summary = findItem(main->contentItem(), QStringLiteral("thisComputer"))));
        const auto saysModels = [summary] {
            QList<QQuickItem *> items{summary};
            while (!items.isEmpty()) {
                QQuickItem *item = items.takeLast();
                items += item->childItems();
                if (item->property("title").toString() == QStringLiteral("This computer")
                    && item->property("help").toString().contains(QStringLiteral("speech model"))) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY(saysModels());
        QVERIFY(!schemaRow(main, QStringLiteral("localRunner")));

        navigation->go(QStringLiteral("models"));
        QTRY_VERIFY(schemaRow(main, QStringLiteral("localRunner")));
        QVERIFY(schemaRow(main, QStringLiteral("localRunnerDetect")));
    }

    // A link to another page, such as Pasting's Change in Apps, offers the
    // way back above the page it opens, until the person goes anywhere else.
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
        navigation->go(QStringLiteral("pasting"));
        QQuickItem *changeInApps = nullptr;
        QTRY_VERIFY((changeInApps = findItem(main->contentItem(), QStringLiteral("changeInApps"))));
        QMetaObject::invokeMethod(changeInApps, "clicked");
        QCOMPARE(navigation->current(), QStringLiteral("apps"));
        QCOMPARE(navigation->returnTo(), QStringLiteral("pasting"));
        const auto backLink = [main] {
            QQuickItem *link = findItem(main->contentItem(), QStringLiteral("backLink"));
            return link && link->isVisible() && link->property("text") == QStringLiteral("Back to Pasting") ? link : nullptr;
        };
        QTRY_VERIFY(backLink());
        QMetaObject::invokeMethod(backLink(), "activated");
        QCOMPARE(navigation->current(), QStringLiteral("pasting"));
        QCOMPARE(navigation->returnTo(), QString());

        // Going anywhere else forgets it, and a search jump is not a link.
        navigation->follow(QStringLiteral("models"));
        QCOMPARE(navigation->returnTo(), QStringLiteral("pasting"));
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

    void setupsServicesLinkReturnsToSetup_data()
    {
        QTest::addColumn<QString>("service");
        QTest::addColumn<QString>("link");
        QTest::addColumn<QString>("page");
        QTest::newRow("server") << QStringLiteral("endpoint") << QStringLiteral("openServices") << QStringLiteral("services");
#ifdef SPEECHER_WITH_LOCAL_SPEECH
        // No model is downloaded, so the card's own button leads there.
        QTest::newRow("this computer") << QStringLiteral("local") << QStringLiteral("openLocalModels") << QStringLiteral("models");
#endif
    }

    // The Fix sheet's way to the whole page, in setup, opens Main on it,
    // whose way back is to setup; once setup closes there is nothing to go
    // back to.
    void setupsServicesLinkReturnsToSetup()
    {
        QFETCH(QString, service);
        QFETCH(QString, link);
        QFETCH(QString, page);
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("speechServices"), test::serviceOrder({service}));
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
        // The service's row in the step's Service Order, which can't run yet.
        QQuickItem *fix = nullptr;
        QTRY_VERIFY((fix = findItem(setup->contentItem(), QStringLiteral("fix_") + service)));
        QMetaObject::invokeMethod(fix, "clicked");
        QQuickItem *open = nullptr;
        QTRY_VERIFY((open = findItem(setup->contentItem(), link)) && open->isVisible());
        QMetaObject::invokeMethod(open, open->metaObject()->indexOfSignal("activated()") >= 0 ? "activated" : "clicked");
        QQuickWindow *main = nullptr;
        QTRY_VERIFY((main = openWindow("Main")));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        QCOMPARE(navigation->current(), page);
        QCOMPARE(navigation->returnTo(), QStringLiteral("setup"));
        QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("backLink"))
                    && findItem(main->contentItem(), QStringLiteral("backLink"))->property("text") == QStringLiteral("Back to setup"));
        navigation->goBack();
        QCOMPARE(navigation->current(), page);
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

    // Every id a link, a search landing or the schema names is a page or a
    // schema row that exists, so nothing lands nowhere after a row or page
    // moves or goes. Ids a page makes up for its own entries (appentry:,
    // vocab:, historyFind: and the like) are the pages' own business.
    void everyIdALinkNamesExists()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        auto *schema = qmlEngine(main)->singletonInstance<QObject *>(QStringLiteral("Speecher.Wording"), QStringLiteral("Schema"));
        QVERIFY(schema);
        QSet<QString> pages;
        for (const QVariant &page : schema->property("pages").value<QJSValue>().toVariant().toList()) {
            pages.insert(page.toMap().value(QStringLiteral("id")).toString());
        }
        QSet<QString> rows;
        const QVariantList rowList = schema->property("rows").value<QJSValue>().toVariant().toList();
        for (const QVariant &row : rowList) {
            rows.insert(row.toMap().value(QStringLiteral("id")).toString());
        }
        QStringList stale;
        const auto check = [&](const QString &where, const QString &id, const QSet<QString> &known) {
            if (!id.isEmpty() && !id.contains(u':') && !known.contains(id)) {
                stale.append(where + QStringLiteral(": ") + id);
            }
        };
        for (const QVariant &value : rowList) {
            const QVariantMap row = value.toMap();
            const QString id = row.value(QStringLiteral("id")).toString();
            check(id + QStringLiteral(" page"), row.value(QStringLiteral("page")).toString(), pages);
            check(id + QStringLiteral(" opens"), row.value(QStringLiteral("opens")).toString(), rows);
            for (const char *when : {"hiddenWhen", "enabledWhen"}) {
                // One condition, or a list of them.
                const QVariant conditions = row.value(QLatin1String(when));
                for (const QVariant &condition : conditions.typeId() == QMetaType::QVariantList ? conditions.toList()
                                                                                                : QVariantList{conditions}) {
                    check(id + QLatin1Char(' ') + QLatin1String(when), condition.toMap().value(QStringLiteral("id")).toString(), rows);
                }
            }
        }
        for (const QString &id : schema->property("suggestions").value<QJSValue>().toVariant().toStringList()) {
            check(QStringLiteral("suggestions"), id, rows);
        }
        for (const QString &id : schema->property("since").value<QJSValue>().toVariant().toMap().keys()) {
            check(QStringLiteral("since"), id, rows);
        }
        // Links and landings written into the QML: Navigation.go("page"),
        // goToRow and follow("page", "row"), rowId: "row" and rowIds: [...].
        static const QRegularExpression link(QStringLiteral("Navigation\\.(?:go|goToRow|follow)\\(\\s*\"([^\"]+)\"(?:\\s*,\\s*\"([^\"]*)\")?"));
        static const QRegularExpression rowId(QStringLiteral("\\browId:\\s*\"([^\"]+)\""));
        static const QRegularExpression rowIds(QStringLiteral("\\browIds:\\s*\\[([^\\]]*)\\]"));
        static const QRegularExpression quoted(QStringLiteral("\"([^\"]+)\""));
        QDirIterator files(QStringLiteral(SPEECHER_SOURCE_DIR "/src/frontend/quick/qml"), {QStringLiteral("*.qml"), QStringLiteral("*.js")},
                           QDir::Files, QDirIterator::Subdirectories);
        int scanned = 0;
        while (files.hasNext()) {
            QFile file(files.next());
            QVERIFY(file.open(QIODevice::ReadOnly));
            // Comments' examples are not links.
            QStringList lines = QString::fromUtf8(file.readAll()).split(u'\n');
            lines.removeIf([](const QString &line) { return line.trimmed().startsWith(QLatin1String("//")); });
            const QString text = lines.join(u'\n');
            const QString where = files.fileName();
            ++scanned;
            for (auto it = link.globalMatch(text); it.hasNext();) {
                const QRegularExpressionMatch match = it.next();
                check(where, match.captured(1), pages);
                check(where, match.captured(2), rows);
            }
            for (auto it = rowId.globalMatch(text); it.hasNext();) {
                check(where, it.next().captured(1), rows);
            }
            for (auto it = rowIds.globalMatch(text); it.hasNext();) {
                const QString list = it.next().captured(1);
                for (auto ids = quoted.globalMatch(list); ids.hasNext();) {
                    check(where, ids.next().captured(1), rows);
                }
            }
        }
        QVERIFY(scanned > 50);
        QVERIFY2(stale.isEmpty(), qPrintable(stale.join(QStringLiteral("\n"))));
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

    // Add service lists the services not in the order with their ratings,
    // so they can be compared before one is added.
    void addServiceShowsTheRatings()
    {
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("speechServices"), test::serviceOrder({QStringLiteral("codex")}));
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("speech"));
        QQuickItem *order = nullptr;
        QTRY_VERIFY((order = findItem(main->contentItem(), QStringLiteral("speechServiceOrder"))));
        const QVariantList available = order->property("available").value<QJSValue>().toVariant().toList();
        const auto claude = std::find_if(available.cbegin(), available.cend(), [](const QVariant &option) {
            return option.toMap().value(QStringLiteral("id")).toString() == QLatin1String("claude");
        });
        QVERIFY(claude != available.cend());
        const QString help = claude->toMap().value(QStringLiteral("help")).toString();
        QVERIFY2(help.startsWith(QLatin1String("Accuracy ")) && help.contains(QLatin1String("out of 10.")), qPrintable(help));
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

    // Pasting lists the App Types and Apps with steps or keys of their own,
    // each with what it does here, and opens one in Apps with its editor
    // open.
    void pastingListsTheAppsThatPasteDifferently()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            draft.apps.apps = {test::idApp(QStringLiteral("org.example.FooEditor"), std::nullopt,
                                           {DeliverySteps{.insert = false, .press = false, .keep = true}, std::nullopt}),
                               test::idApp(QStringLiteral("org.example.Steps"))};
            draft.apps.types[5].pasting.steps = DeliverySteps{.insert = true, .press = false, .keep = true};
            return true;
        });
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        navigation->go(QStringLiteral("pasting"));
        QQuickItem *app = nullptr;
        QTRY_VERIFY((app = findItem(main->contentItem(), QStringLiteral("differently_appentry:app:org.example.FooEditor"))));
        QVERIFY(findItem(main->contentItem(), QStringLiteral("differently_apptype:ai_coding")));
        QVERIFY(findItem(main->contentItem(), QStringLiteral("differently_apptype:terminal")) == nullptr || terminalKeysDiffer);
        // Following them is no different from them.
        QVERIFY(!findItem(main->contentItem(), QStringLiteral("differently_appentry:app:org.example.Steps")));
        QQuickItem *result = findItem(app, QStringLiteral("appLinkDetail"));
        QVERIFY(result);
        QCOMPARE(result->property("text").toString(), QStringLiteral("Clipboard only"));

        QMetaObject::invokeMethod(app, "clicked");
        QTRY_COMPARE(navigation->current(), QStringLiteral("apps"));
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(main->contentItem(), QStringLiteral("appentry:app:org.example.FooEditor"))));
        QTRY_COMPARE(navigation->pendingRow(), QString());
        QTRY_VERIFY(row->parentItem()->property("open").toBool());
        QCOMPARE(navigation->returnTo(), QStringLiteral("pasting"));
    }

    // A dropdown's menu is as wide as its options need, so a long "Same as
    // …" shows whole beside its check, and one with no options says why
    // rather than opening as an empty sliver.
    void dropdownsShowTheirOptionsWhole()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            draft.apps.apps = {test::idApp(QStringLiteral("org.example.FooEditor"))};
            return true;
        });
        QuickSettings(&controller).set(QStringLiteral("speechServices"),
                                       test::serviceOrder({QStringLiteral("claude"), QStringLiteral("local")}));
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);
        const auto popupOf = [](QQuickItem *select) { return select->property("popup").value<QObject *>(); };
        const auto rows = [](QObject *popup) {
            auto *list = popup->property("contentItem").value<QQuickItem *>();
            auto *content = list ? list->property("contentItem").value<QQuickItem *>() : nullptr;
            return content ? content->childItems() : QList<QQuickItem *>();
        };
        QQuickItem *pasting = nullptr;
        QQuickItem *model = nullptr;
        runSteps({
            {QStringLiteral("an App's Pasting menu"), [&] {
                 navigation->goToRow(QStringLiteral("apps"), QStringLiteral("appentry:app:org.example.FooEditor"));
                 QTRY_VERIFY((pasting = findItem(main->contentItem(), QStringLiteral("appPasting"))));
                 QTRY_VERIFY(pasting->isVisible());
                 QVERIFY(QMetaObject::invokeMethod(popupOf(pasting), "open"));
                 QTRY_VERIFY(popupOf(pasting)->property("opened").toBool());
                 const QList<QQuickItem *> items = rows(popupOf(pasting));
                 QVERIFY(!items.isEmpty());
                 for (QQuickItem *item : items) {
                     if (!item->isVisible()) {
                         continue;
                     }
                     QVERIFY2(item->width() >= item->implicitWidth(),
                              qPrintable(item->property("text").toString() + QStringLiteral(" is cut short")));
                 }
             }},
            {QStringLiteral("This computer's model picker with nothing downloaded"), [&] {
                 QVERIFY(QMetaObject::invokeMethod(popupOf(pasting), "close"));
                 navigation->go(QStringLiteral("speech"));
                 QTRY_VERIFY((model = [&]() -> QQuickItem * {
                     QList<QQuickItem *> items{main->contentItem()};
                     while (!items.isEmpty()) {
                         QQuickItem *item = items.takeLast();
                         items += item->childItems();
                         if (item->objectName() == QLatin1String("serviceModel") && item->property("service").toString() == QLatin1String("local")) {
                             return item->property("item").value<QQuickItem *>();
                         }
                     }
                     return nullptr;
                 }()));
                 QVERIFY(!model->property("emptyText").toString().isEmpty());
                 QVERIFY(QMetaObject::invokeMethod(popupOf(model), "open"));
                 QTRY_VERIFY(popupOf(model)->property("opened").toBool());
                 QVERIFY(popupOf(model)->property("height").toReal() > 40);
             }},
        }, main);
        QVERIFY(QMetaObject::invokeMethod(popupOf(model), "close"));
    }

    // A saved App is found by its name or what it is recognized by, and the
    // result opens it on Apps.
    // Your own writing profiles, tones and cleanup levels are found by name:
    // a profile opens in the editor, a tone or level in its edit form.
    void searchFindsYourProfilesTonesAndLevels()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            WritingProfileSettings lab;
            lab.profile = QStringLiteral("custom_profile_lab");
            lab.name = QStringLiteral("Lab notebook");
            draft.refinement.writingProfiles.append(lab);
            draft.refinement.customTones = {{QStringLiteral("custom_tone_dry"), QStringLiteral("Deadpan"), QStringLiteral("Short and dry.")}};
            draft.refinement.customCleanupLevels = {
                {QStringLiteral("custom_level_minutes"), QStringLiteral("Minutes"), QStringLiteral("balanced"), QStringLiteral("Bullets.")}};
            return true;
        });
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
        QCOMPARE(searchResults(search, QStringLiteral("lab notebook")).first(),
                 std::make_pair(QStringLiteral("profile:custom_profile_lab"), QStringLiteral("Refinement › Writing profiles, Profiles")));
        QCOMPARE(searchResults(search, QStringLiteral("deadpan")).first().first, QStringLiteral("tone:custom_tone_dry"));
        QCOMPARE(searchResults(search, QStringLiteral("minutes")).first().first, QStringLiteral("level:custom_level_minutes"));

        searchResults(search, QStringLiteral("lab notebook"));
        QMetaObject::invokeMethod(search, "activate", Q_ARG(QVariant, 0));
        QTRY_COMPARE(navigation->current(), QStringLiteral("profiles"));
        QQuickItem *page = nullptr;
        QTRY_VERIFY((page = findItem(main->contentItem(), QStringLiteral("profilesPage"))));
        QTRY_COMPARE(page->property("selectedId").toString(), QStringLiteral("custom_profile_lab"));

        QMetaObject::invokeMethod(search, "open");
        searchResults(search, QStringLiteral("deadpan"));
        QMetaObject::invokeMethod(search, "activate", Q_ARG(QVariant, 0));
        QQuickItem *form = nullptr;
        QTRY_VERIFY((form = findItem(page, QStringLiteral("toneForm"))));
        QTRY_VERIFY(form->property("open").toBool());
        QCOMPARE(form->property("editing").toString(), QStringLiteral("custom_tone_dry"));
    }

    // Search History is an offer, not a match: words no setting has say so,
    // and the count leaves it out.
    void searchSaysWhenNoSettingMatches()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        auto *settings = qmlEngine(main)->singletonInstance<QuickSettings *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Settings"));
        auto *search = main->findChild<QObject *>(QStringLiteral("searchPalette"));
        QVERIFY(settings && search);
        settings->set(QStringLiteral("historyKeeps"), QStringLiteral("text"));
        QMetaObject::invokeMethod(search, "open");
        QCOMPARE(searchResults(search, QStringLiteral("zzz no match")).size(), 1);
        QCOMPARE(search->property("_matches").toInt(), 0);
        searchResults(search, QStringLiteral("theme"));
        QVERIFY(search->property("_matches").toInt() > 0);
    }

    void searchFindsASavedAppAndOpensIt()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            draft.apps.apps = {test::idApp(QStringLiteral("org.example.FooEditor"))};
            return true;
        });
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        auto *search = main->findChild<QObject *>(QStringLiteral("searchPalette"));
        QVERIFY(navigation && search);
        const QString rowId = QStringLiteral("appentry:app:org.example.FooEditor");
        QMetaObject::invokeMethod(search, "open");
        const auto results = searchResults(search, QStringLiteral("fooeditor"));
        QVERIFY(!results.isEmpty());
        QCOMPARE(results.first(), std::make_pair(rowId, QStringLiteral("Apps, Your apps")));
        // The old words still find where the settings went.
        const auto defaults = searchResults(search, QStringLiteral("unrecognized"));
        QVERIFY(std::any_of(defaults.cbegin(), defaults.cend(),
                            [](const auto &result) { return result.first == QLatin1String("otherAppsProfile"); }));
        QCOMPARE(searchResults(search, QStringLiteral("fooeditor")).first().first, rowId);
        QTest::keyClick(main, Qt::Key_Return);
        QTRY_COMPARE(navigation->current(), QStringLiteral("apps"));
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(main->contentItem(), rowId)));
        QTRY_COMPARE(navigation->pendingRow(), QString());
        QTRY_VERIFY(row->parentItem()->property("open").toBool());
        QTRY_VERIFY(row->mapToScene(QPointF(0, 0)).y() >= 0 && row->mapToScene(QPointF(0, row->height())).y() <= main->height());
    }

    // Matching by window title adds an App and opens its editor, which says
    // what it is recognized by and leaves out the type; removing it offers
    // an undo that works once the page is gone.
    void anAppAddedByTitleOpensAndRemovesWithUndo()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        QQmlEngine *engine = qmlEngine(main);
        auto *navigation = engine->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        auto *apps = engine->singletonInstance<QuickApps *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Apps"));
        auto *toaster = engine->singletonInstance<QObject *>(QStringLiteral("Speecher.Controls"), QStringLiteral("Toaster"));
        QVERIFY(navigation && apps && toaster);
        navigation->go(QStringLiteral("apps"));

        const QVariantMap added = apps->add({{QStringLiteral("name"), QStringLiteral("Gmail")},
                                             {QStringLiteral("matches"), QVariantList{QVariantMap{
                                                  {QStringLiteral("kind"), QStringLiteral("title")},
                                                  {QStringLiteral("value"), QStringLiteral("Gmail")}}}}});
        const QString id = added.value(QStringLiteral("id")).toString();
        QVERIFY2(!id.isEmpty(), qPrintable(added.value(QStringLiteral("refusal")).toString()));
        QCOMPARE(apps->add({{QStringLiteral("name"), QStringLiteral("gmail")},
                            {QStringLiteral("matches"), QVariantList{QVariantMap{{QStringLiteral("kind"), QStringLiteral("title")},
                                                                                 {QStringLiteral("value"), QStringLiteral("GMAIL")}}}}})
                     .value(QStringLiteral("refusal")),
                 QVariant(QStringLiteral("taken")));
        navigation->goToRow(QStringLiteral("apps"), QStringLiteral("appentry:") + id);
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(main->contentItem(), QStringLiteral("appentry:") + id)));
        QTRY_VERIFY(row->parentItem()->property("open").toBool());
        // Its own steps start from the ones it follows, the Pasting page's;
        // then each switch is its own, and the last one on stays on.
        QQuickItem *pasting = findItem(row->parentItem(), QStringLiteral("appPasting"));
        QVERIFY(pasting);
        QMetaObject::invokeMethod(pasting, "chosen", Q_ARG(QString, QStringLiteral("own")));
        QCOMPARE(controller.settings()->snapshot().apps.apps.first().pasting.steps, DeliverySteps{});
        for (const char *step : {"insert", "press"}) {
            QQuickItem *toggle = nullptr;
            QTRY_VERIFY((toggle = findItem(row->parentItem(), QStringLiteral("stepSwitch_") + QLatin1String(step))));
            toggle->setProperty("checked", false);
            QMetaObject::invokeMethod(toggle, "toggled");
        }
        QCOMPARE(controller.settings()->snapshot().apps.apps.first().pasting.steps,
                 (DeliverySteps{.insert = false, .press = false, .keep = true}));
        QTRY_VERIFY(!findItem(row->parentItem(), QStringLiteral("stepSwitch_keep"))->property("enabled").toBool());
        // Each of its own steps explains itself as Pasting's does.
        QQuickItem *press = schemaRow(row->parentItem(), QStringLiteral("pressPasteKeys"));
        QVERIFY(press);
        QVERIFY(press->property("help").toString().contains(QStringLiteral("Copies your words and presses")));
        QMetaObject::invokeMethod(pasting, "chosen", Q_ARG(QString, QString()));
        QCOMPARE(controller.settings()->snapshot().apps.apps.first().pasting, PastingChoice{});
        // Name and recognition can change in place, keeping the App.
        QCOMPARE(apps->update(id, {{QStringLiteral("name"), QStringLiteral("Gmail in Firefox")},
                                   {QStringLiteral("matches"), QVariantList{QVariantMap{
                                        {QStringLiteral("kind"), QStringLiteral("title")},
                                        {QStringLiteral("value"), QStringLiteral("Inbox - Gmail")}}}}}),
                 QString());
        QCOMPARE(controller.settings()->snapshot().apps.apps.first().id, id);
        QCOMPARE(controller.settings()->snapshot().apps.apps.first().matches,
                 (QList<AppMatch>{{AppMatchKind::WindowTitle, QStringLiteral("Inbox - Gmail")}}));
        QCOMPARE(apps->update(id, {{QStringLiteral("matches"), QVariantList{}}}), QStringLiteral("blank"));
        QCOMPARE(apps->update(id, {{QStringLiteral("type"), QStringLiteral("email")}}), QString());

        QSignalSpy posted(toaster, SIGNAL(posted(QVariant)));
        const int undo = apps->remove(id);
        QVERIFY(undo >= 0);
        QVERIFY(controller.settings()->snapshot().apps.apps.isEmpty());
        navigation->go(QStringLiteral("home"));
        apps->undo(undo);
        QCOMPARE(controller.settings()->snapshot().apps.apps.size(), 1);
        QCOMPARE(controller.settings()->snapshot().apps.apps.first().type, AppCategory::Email);
    }

    // An App's History choice can only lower what Privacy & data keeps. Once
    // chosen, Privacy & data lists the App, and its row opens it in Apps.
    void anAppKeptOutOfHistoryShowsInPrivacyAndData()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            draft.apps.apps = {test::idApp(QStringLiteral("org.example.Vault"))};
            return true;
        });
        const QString id = QStringLiteral("app:org.example.Vault");
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        QQmlEngine *engine = qmlEngine(main);
        auto *navigation = engine->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        auto *apps = engine->singletonInstance<QuickApps *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Apps"));
        QVERIFY(navigation && apps);
        QTest::ignoreMessage(QtWarningMsg, "Apps.update takes no History choice text");
        QCOMPARE(apps->update(id, {{QStringLiteral("history"), QStringLiteral("text")}}), QStringLiteral("invalid"));

        navigation->goToRow(QStringLiteral("apps"), QStringLiteral("appentry:") + id);
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(main->contentItem(), QStringLiteral("appentry:") + id)));
        QTRY_VERIFY(row->parentItem()->property("open").toBool());
        QQuickItem *history = findItem(row->parentItem(), QStringLiteral("appHistory"));
        QVERIFY(history);
        QVERIFY(QMetaObject::invokeMethod(history, "chosen", Q_ARG(QString, QStringLiteral("nothing"))));
        QCOMPARE(controller.settings()->snapshot().apps.apps.first().history, HistoryKeep::Nothing);

        navigation->go(QStringLiteral("privacy"));
        QQuickItem *listed = nullptr;
        QTRY_VERIFY((listed = findItem(main->contentItem(), QStringLiteral("leftOut_appentry:") + id)));
        QVERIFY(QMetaObject::invokeMethod(listed, "clicked"));
        QTRY_COMPARE(navigation->current(), QStringLiteral("apps"));
        QTRY_VERIFY((row = findItem(main->contentItem(), QStringLiteral("appentry:") + id)));
        QTRY_VERIFY(row->parentItem()->property("open").toBool());
        QCOMPARE(navigation->returnTo(), QStringLiteral("privacy"));
    }

    // Scanning for installed apps says when it ends, even when it finds
    // nothing more to list.
    void anAppScanSaysWhenItEnds()
    {
        ApplicationController controller(true);
        QuickApps apps(&controller);
        QSignalSpy scanning(&apps, &QuickApps::scanningChanged);
        apps.refresh();
        QVERIFY(apps.scanning());
        QTRY_VERIFY_WITH_TIMEOUT(!apps.scanning(), 10000);
        QCOMPARE(scanning.count(), 2);
    }

    // Typing in an App's match field keeps every character, the cursor
    // staying in the field.
    void typingAnAppsMatchKeepsEveryCharacter()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            draft.apps.apps = {test::idApp(QStringLiteral("org.example.Vault"))};
            return true;
        });
        const QString id = QStringLiteral("app:org.example.Vault");
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        main->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"),
                                                                                QStringLiteral("Navigation"));
        navigation->goToRow(QStringLiteral("apps"), QStringLiteral("appentry:") + id);
        QQuickItem *row = nullptr;
        QTRY_VERIFY((row = findItem(main->contentItem(), QStringLiteral("appentry:") + id)));
        QTRY_VERIFY(row->parentItem()->property("open").toBool());
        QVERIFY(QMetaObject::invokeMethod(findItem(row->parentItem(), QStringLiteral("appEditMatching")), "clicked"));
        QPointer<QQuickItem> field;
        QTRY_VERIFY((field = findItem(row->parentItem(), QStringLiteral("appMatchValue_0"))));
        field->forceActiveFocus();
        QTRY_VERIFY(field->hasActiveFocus());
        QTest::keyClick(main, Qt::Key_End);
        for (const char key : QByteArrayLiteral(".Beta")) {
            QTest::keyClick(main, key);
        }
        QTest::qWait(50);
        QVERIFY(field);
        QCOMPARE(field->property("text").toString(), QStringLiteral("org.example.Vault.Beta"));
        // Removing the row takes it without a stale read of the list.
        QQuickItem *editor = field;
        while (!editor->property("draftMatches").isValid()) {
            editor = editor->parentItem();
        }
        editor->setProperty("draftMatches", QVariantList{});
        QTRY_VERIFY(!findItem(row->parentItem(), QStringLiteral("appMatchValue_0")));
    }

    // Choosing to keep less asks about the text kept; choosing something
    // else before answering drops the question, so its answer can't save
    // the first choice.
    void aNewKeepChoiceDropsTheQuestion()
    {
        ApplicationController controller(true);
        controller.history()->clear({}, {}, true);
        controller.settings()->edit([](AppSettings &draft) {
            draft.history.keeps = HistoryKeep::Text;
            return true;
        });
        controller.history()->add(keptDictation(QStringLiteral("Kept words.")));
        const auto forget = qScopeGuard([&controller] { controller.history()->clear({}, {}, true); });
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main && QTest::qWaitForWindowExposed(main));
        qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"))
            ->go(QStringLiteral("privacy"));
        QQuickItem *page = nullptr;
        QQuickItem *keeps = nullptr;
        QTRY_VERIFY((page = findItem(main->contentItem(), QStringLiteral("privacyPage"))));
        QTRY_VERIFY((keeps = findItem(page, QStringLiteral("historyKeeps"))));

        QVERIFY(QMetaObject::invokeMethod(keeps, "activated", Q_ARG(QString, QStringLiteral("stats"))));
        QCOMPARE(page->property("_lowerTo").toString(), QStringLiteral("stats"));
        QVERIFY(QMetaObject::invokeMethod(keeps, "activated", Q_ARG(QString, QStringLiteral("audio"))));
        QCOMPARE(page->property("_lowerTo").toString(), QString());
        QCOMPARE(controller.settings()->snapshot().history.keeps, HistoryKeep::Audio);
    }

    // A batch's own Spoken Language shows as chosen, not the one it would
    // inherit.
    void aBatchShowsItsOwnLanguage()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main && QTest::qWaitForWindowExposed(main));
        QQmlEngine *engine = qmlEngine(main);
        engine->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"))
            ->go(QStringLiteral("transcribe"));
        QQuickItem *language = nullptr;
        QTRY_VERIFY((language = findItem(main->contentItem(), QStringLiteral("batchLanguage"))));
        QCOMPARE(language->property("shownId").toString(), QStringLiteral("en"));
        QObject *transcription = engine->singletonInstance<QObject *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Transcription"));
        QVERIFY(QMetaObject::invokeMethod(transcription, "setOption", Q_ARG(QString, QStringLiteral("language")),
                                          Q_ARG(QVariant, QStringLiteral("fr"))));
        QCOMPARE(language->property("shownId").toString(), QStringLiteral("fr"));
    }

    // Recordings' help names each speech service once: as able to record
    // calls, or not.
    void recordingsNamesEachServiceOnce()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main && QTest::qWaitForWindowExposed(main));
        qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"))
            ->go(QStringLiteral("recordings"));
        QString help;
        QTRY_VERIFY(!(help = rowHelp(main->contentItem(), QStringLiteral(" can record calls."))).isEmpty());
        const QString cannot = help.section(QStringLiteral(" can record calls. "), 1);
        QVERIFY2(!cannot.isEmpty(), qPrintable(help));
        QVERIFY2(!cannot.contains(QLatin1String("ChatGPT")), qPrintable(help));
    }

    // Ctrl+F has one owner: it goes to History's search, or its reader's
    // Find while a recording is open there, and opens the search palette on
    // a page without one. Search History for … closes the reader.
    void findGoesToThePagesSearchOrThePalette()
    {
        ApplicationController controller(true);
        controller.history()->clear({}, {}, true);
        forgetRecordings(controller);
        const auto forget = qScopeGuard([&controller] {
            controller.history()->clear({}, {}, true);
            forgetRecordings(controller);
        });
        controller.history()->add(keptDictation(QStringLiteral("Book the board room.")));
        HistoryEntry meeting;
        meeting.kind = HistoryKind::Recording;
        meeting.startedAt = QDateTime::currentDateTime().addDays(-1);
        meeting.kept = HistoryKeep::Text;
        meeting.outcome = HistoryOutcome::Complete;
        const qint64 meetingId = controller.history()->add(meeting);
        controller.history()->appendLine(meetingId, {4000, QStringLiteral("me"), QStringLiteral("Can everyone see the board?")});
        controller.history()->finishRecording(meetingId, meeting.startedAt.addSecs(60), 60 * 1000, {}, {});
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        main->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"),
                                                                                QStringLiteral("Navigation"));
        auto *palette = main->findChild<QObject *>(QStringLiteral("searchPalette"));
        QVERIFY(palette);
        const auto find = [main] {
            main->contentItem()->forceActiveFocus();
            QTest::keyClick(main, Qt::Key_F, Qt::ControlModifier);
        };

        navigation->go(QStringLiteral("history"));
        QQuickItem *search = nullptr;
        QTRY_VERIFY((search = findItem(main->contentItem(), QStringLiteral("historySearch"))));
        find();
        QTRY_VERIFY(search->hasActiveFocus());

        navigation->goToRow(QStringLiteral("history"), QStringLiteral("recording:") + QString::number(meetingId));
        QQuickItem *reader = nullptr;
        QTRY_VERIFY((reader = findItem(main->contentItem(), QStringLiteral("transcriptFind"))) && reader->isVisible());
        find();
        QTRY_VERIFY(reader->hasActiveFocus());

        navigation->goToRow(QStringLiteral("history"), QStringLiteral("historyFind:board"));
        QQuickItem *page = search;
        while (!page->property("readingId").isValid()) {
            page = page->parentItem();
        }
        QCOMPARE(page->property("readingId").toInt(), 0);
        QCOMPARE(search->property("text").toString(), QStringLiteral("board"));

        navigation->go(QStringLiteral("general"));
        QVERIFY(!palette->property("shown").toBool());
        find();
        QTRY_VERIFY(palette->property("shown").toBool());
    }

    // Refine again in a reader that moves to another entry starts over, so
    // Keep this can't save one entry's refined text into the next; the same
    // entry read again, as History refreshes, keeps what it has.
    void refineAgainStartsOverForAnotherEntry()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QQmlComponent component(qmlEngine(main), QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/pages/history/RefineAgain.qml")));
        const auto entry = [](qint64 id) { return QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("profile"), QString()}}; };
        const std::unique_ptr<QObject> again(component.createWithInitialProperties({{QStringLiteral("entry"), entry(1)}}));
        QVERIFY2(again, qPrintable(component.errorString()));
        again->setProperty("refinedText", QStringLiteral("refined first"));
        again->setProperty("refining", true);
        again->setProperty("failed", true);

        again->setProperty("entry", entry(1));
        QCOMPARE(again->property("refinedText").toString(), QStringLiteral("refined first"));

        again->setProperty("entry", entry(2));
        QVERIFY(again->property("refinedText").toString().isEmpty());
        QVERIFY(!again->property("refining").toBool());
        QVERIFY(!again->property("failed").toBool());
    }

    // While History keeps text, search ends with Search History for the
    // words, which opens History with them searched; keeping only stats, it
    // doesn't offer it.
    void searchOffersHistoryWhileItKeepsText()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        QQmlEngine *engine = qmlEngine(main);
        auto *navigation = engine->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        auto *settings = engine->singletonInstance<QuickSettings *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Settings"));
        auto *search = main->findChild<QObject *>(QStringLiteral("searchPalette"));
        QVERIFY(navigation && settings && search);
        const auto offered = [search] {
            const auto results = searchResults(search, QStringLiteral("quarterly deck"));
            return !results.isEmpty() && results.last() == std::pair{QStringLiteral("historyFind:quarterly deck"), QStringLiteral("Home › History")};
        };
        settings->set(QStringLiteral("historyKeeps"), QStringLiteral("stats"));
        QMetaObject::invokeMethod(search, "open");
        QVERIFY(!offered());
        settings->set(QStringLiteral("historyKeeps"), QStringLiteral("text"));
        QVERIFY(offered());

        const int last = int(search->property("results").value<QJSValue>().property(QStringLiteral("length")).toInt()) - 1;
        QMetaObject::invokeMethod(search, "activate", Q_ARG(QVariant, last));
        QTRY_COMPARE(navigation->current(), QStringLiteral("history"));
        QQuickItem *field = nullptr;
        QTRY_VERIFY((field = findItem(main->contentItem(), QStringLiteral("historySearch"))));
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("quarterly deck"));
        QCOMPARE(navigation->pendingRow(), QString());
    }

    // History in each of its states: off, keeping stats, nothing kept yet,
    // then a seed's entries, a search that matches nothing, and an entry
    // opened in place.
    void historyStatesLoad_data() { themes(); }
    void historyStatesLoad()
    {
        QFETCH(QString, theme);
        qputenv("SPEECHER_INSIGHTS_TODAY", "2026-11-08");
        const auto unpin = qScopeGuard([] {
            qunsetenv("SPEECHER_INSIGHTS_TODAY");
            qunsetenv("SPEECHER_HISTORY_SEED");
        });
        const auto rows = [](QQuickItem *root) {
            QList<QQuickItem *> found;
            QList<QQuickItem *> items{root};
            while (!items.isEmpty()) {
                QQuickItem *item = items.takeLast();
                items += item->childItems();
                if (item->objectName().startsWith(QLatin1String("historyRow_")) && item->isVisible()) {
                    found.append(item);
                }
            }
            return found;
        };
        {
            ApplicationController controller(true);
            controller.history()->clear({}, {}, true);
            QuickSettings settings(&controller);
            settings.set(QStringLiteral("themeControl"), theme);
            TrayIcon tray(&controller);
            QuickFrontEnd ui(&controller, &tray);
            ui.showMainWindow();
            QQuickWindow *main = openWindow("Main");
            QVERIFY(main && QTest::qWaitForWindowExposed(main));
            auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"),
                                                                                    QStringLiteral("Navigation"));
            QList<std::pair<QString, std::function<void()>>> steps;
            for (const QString keeps : {QStringLiteral("nothing"), QStringLiteral("stats"), QStringLiteral("text")}) {
                steps.append({QStringLiteral("history keeps ") + keeps, [&settings, navigation, keeps, main, rows] {
                                  settings.set(QStringLiteral("historyKeeps"), keeps);
                                  navigation->go(QStringLiteral("history"));
                                  QCOMPARE(navigation->current(), QStringLiteral("history"));
                                  QVERIFY(rows(main->contentItem()).isEmpty());
                              }});
            }
            runSteps(steps, main);
        }
        if (QTest::currentTestFailed()) {
            return;
        }

        qputenv("SPEECHER_HISTORY_SEED", QFINDTESTDATA("data/history/seed.json").toUtf8());
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("themeControl"), theme);
        settings.set(QStringLiteral("historyKeeps"), QStringLiteral("text"));
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main && QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"),
                                                                                QStringLiteral("Navigation"));
        QQuickItem *search = nullptr;
        runSteps({
            {QStringLiteral("history results"), [navigation, main, rows, &search] {
                 navigation->go(QStringLiteral("history"));
                 QTRY_COMPARE(rows(main->contentItem()).size(), 8);
                 QVERIFY((search = findItem(main->contentItem(), QStringLiteral("historySearch"))));
             }},
            {QStringLiteral("history search"), [main, rows, &search] {
                 search->setProperty("text", QStringLiteral("deck"));
                 QTRY_COMPARE(rows(main->contentItem()).size(), 1);
             }},
            {QStringLiteral("history no match"), [main, rows, &search] {
                 search->setProperty("text", QStringLiteral("zzz no such words"));
                 QTRY_VERIFY(rows(main->contentItem()).isEmpty());
             }},
            {QStringLiteral("history open entry"), [main, rows, &search] {
                 search->setProperty("text", QString());
                 QTRY_COMPARE(rows(main->contentItem()).size(), 8);
                 QQuickItem *newest = findItem(main->contentItem(), QStringLiteral("historyRow_8"));
                 QVERIFY(newest);
                 QMetaObject::invokeMethod(newest, "clicked");
                 QTRY_VERIFY(newest->property("selected").toBool());
             }},
            // Recordings' "All recordings in History" lands filtered to them.
            {QStringLiteral("history of recordings"), [navigation, main, rows] {
                 navigation->goToRow(QStringLiteral("history"), QStringLiteral("historyKind:recording"));
                 QTRY_COMPARE(rows(main->contentItem()).size(), 1);
                 QCOMPARE(findItem(main->contentItem(), QStringLiteral("historyKind"))->property("currentId").toString(),
                          QStringLiteral("recording"));
             }},
            {QStringLiteral("home with history"), [navigation] { navigation->go(QStringLiteral("home")); }},
        },
                 main);
    }

    // Recordings in each state of a real recording, over fake inputs and a
    // fake speech service: idle with a recent one, refused while no speech
    // service can record, starting, recording with call audio reconnecting,
    // and stopping; then the popup's pill while it records and once it is
    // saved.
    void recordingStatesLoad_data() { themes(); }
    void recordingStatesLoad()
    {
        QFETCH(QString, theme);
        const auto platform = std::make_shared<RecordingPlatform>();
        ApplicationController controller(true, platform);
        controller.history()->clear({}, {}, true);
        forgetRecordings(controller);
        const auto forget = qScopeGuard([&controller] { forgetRecordings(controller); });
        QList<QPointer<test::FakeSpeechTranscriber>> streams;
        bool connects = false;
        controller.providerRegistry()->registerSpeechProvider(
            {QStringLiteral("codex"), QStringLiteral("ChatGPT")}, [&streams, &connects](QObject *parent) {
                auto *codex = new test::FakeSpeechTranscriber(parent);
                codex->providerId = QStringLiteral("codex");
                codex->streamsFinals = true;
                codex->autoCompleteOnFinish = false;
                codex->onStartAttempt = [codex, &connects] {
                    if (connects) {
                        codex->emitConnected();
                    }
                };
                streams << codex;
                return codex;
            });
        QTemporaryDir folder;
        controller.settings()->edit([&folder](AppSettings &draft) {
            draft.setupCompleted = true;
            draft.speech.services.order = {QStringLiteral("local")};
            draft.recording.folder = folder.path();
            return true;
        });
        controller.settings()->setRecordingConsentNoticeShown(true);
        // A recording from yesterday, for Recent recordings.
        HistoryEntry past;
        past.kind = HistoryKind::Recording;
        past.startedAt = QDateTime::currentDateTime().addDays(-1);
        past.title = QStringLiteral("Design review");
        past.transcriptPath = folder.filePath(QStringLiteral("design-review.md"));
        past.kept = HistoryKeep::Text;
        past.outcome = HistoryOutcome::Complete;
        const qint64 pastId = controller.history()->add(past);
        controller.history()->appendLine(pastId, {4000, QStringLiteral("me"), QStringLiteral("Can everyone see the board?")});
        controller.history()->finishRecording(pastId, past.startedAt.addSecs(18 * 60), 18 * 60 * 1000, {}, {});
        QuickSettings settings(&controller);
        settings.set(QStringLiteral("themeControl"), theme);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main && QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"),
                                                                                QStringLiteral("Navigation"));
        RecordingSession *recording = controller.recording();
        const auto streamsRunning = [&streams] {
            return std::count_if(streams.cbegin(), streams.cend(),
                                 [](const QPointer<test::FakeSpeechTranscriber> &s) { return s && s->startCalls > 0; });
        };
        runSteps({
            {QStringLiteral("recordings refused"), [navigation, main] {
                 navigation->go(QStringLiteral("recordings"));
                 QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("recordingRefusal")));
             }},
            {QStringLiteral("recordings idle"), [&controller, main] {
                 controller.settings()->edit([](AppSettings &draft) {
                     draft.speech.services.order = {QStringLiteral("codex"), QStringLiteral("local")};
                     return true;
                 });
                 QQuickItem *record = findItem(main->contentItem(), QStringLiteral("recordButton"));
                 QVERIFY(record);
                 QTRY_VERIFY(record->isEnabled());
             }},
            {QStringLiteral("recordings starting"), [main, recording, &streamsRunning] {
                 findItem(main->contentItem(), QStringLiteral("recordingName"))->setProperty("text", QStringLiteral("Weekly sync"));
                 QMetaObject::invokeMethod(findItem(main->contentItem(), QStringLiteral("recordButton")), "clicked");
                 QTRY_COMPARE(recording->phase(), RecordingSession::Phase::Starting);
                 QTRY_COMPARE(streamsRunning(), 2);
             }},
            {QStringLiteral("recordings recording, call audio reconnecting"), [&] {
                 for (const QPointer<test::FakeSpeechTranscriber> &stream : std::as_const(streams)) {
                     if (stream && stream->startCalls > 0) {
                         stream->emitConnected();
                     }
                 }
                 QTRY_COMPARE(recording->phase(), RecordingSession::Phase::Recording);
                 test::FakeSpeechTranscriber *me = streams.at(streams.size() - 2);
                 test::FakeSpeechTranscriber *them = streams.last();
                 me->emitFinalText(QStringLiteral("So the fix is on the branch already."));
                 them->emitFinalText(QStringLiteral("Great, can you link it in the channel?"));
                 me->emitFinalText(QStringLiteral("Sure, give me a second."));
                 emit platform->microphone->levelChanged(0.6f);
                 emit platform->systemAudio->levelChanged(0.3f);
                 connects = false;
                 them->emitFailure(QStringLiteral("Connection reset"), true);
                 QTRY_COMPARE(recording->status().streams.last().state, RecordingStreamStatus::State::Reconnecting);
                 QCOMPARE(recording->status().title, QStringLiteral("Weekly sync"));
             }},
            {QStringLiteral("home while recording"), [navigation, main] {
                 navigation->go(QStringLiteral("home"));
                 QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("homeRecording")));
             }},
            {QStringLiteral("recordings stopping"), [navigation, main, recording] {
                 navigation->go(QStringLiteral("recordings"));
                 QQuickItem *stop = nullptr;
                 QTRY_VERIFY((stop = findItem(main->contentItem(), QStringLiteral("stopRecording"))));
                 QMetaObject::invokeMethod(stop, "clicked");
                 QTRY_COMPARE(recording->phase(), RecordingSession::Phase::Stopping);
             }},
        },
                 main);
        if (QTest::currentTestFailed()) {
            return;
        }

        // The pill, over a fresh recording, then saved once it stops.
        QQuickWindow *popup = openWindow("PopupWindow");
        QVERIFY(popup);
        const auto grabPopup = [popup, &theme](const QString &step) {
            if (captureDir().isEmpty()) {
                return;
            }
            QTest::qWait(kSettleMs);
            QVERIFY(QDir().mkpath(captureDir()));
            QVERIFY(popup->grabWindow().save(QDir(captureDir()).filePath(theme + QStringLiteral("-popup-") + step + QStringLiteral(".png"))));
        };
        for (const QPointer<test::FakeSpeechTranscriber> &stream : std::as_const(streams)) {
            if (stream && stream->startCalls > 0) {
                stream->emitCompletion();
            }
        }
        QTRY_COMPARE(recording->phase(), RecordingSession::Phase::Off);
        QQuickItem *pill = findItem(popup->contentItem(), QStringLiteral("recordingPill"));
        QVERIFY(pill);
        QTRY_VERIFY(popup->isVisible());
        QTRY_COMPARE(findItem(popup->contentItem(), QStringLiteral("recordingPillText"))->property("text").toString(),
                     QStringLiteral("Saved"));
        grabPopup(QStringLiteral("saved"));
        connects = true;
        QuickRecording *quick = qmlEngine(main)->singletonInstance<QuickRecording *>(QStringLiteral("Speecher.Backend"),
                                                                                     QStringLiteral("Recording"));
        quick->start(QString());
        QTRY_COMPARE(recording->phase(), RecordingSession::Phase::Recording);
        QTRY_VERIFY(findItem(popup->contentItem(), QStringLiteral("recordingPillText"))
                        ->property("text").toString().startsWith(QStringLiteral("Recording ")));
        QVERIFY(pill->property("shown").toBool());
        grabPopup(QStringLiteral("recording"));
        // Recent recordings lists both, newest first, in History's rows.
        QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("historyRow_") + QString::number(pastId)));
        for (const QPointer<test::FakeSpeechTranscriber> &stream : std::as_const(streams)) {
            if (stream) {
                stream->autoCompleteOnFinish = true;
            }
        }
        recording->stop();
        QTRY_COMPARE(recording->phase(), RecordingSession::Phase::Off);

        // In History, a recording is a row; a search lists its lines that
        // match, and opening it reads it.
        QQuickItem *search = nullptr;
        runSteps({
            {QStringLiteral("history with recordings"), [navigation, main, &search, pastId] {
                 navigation->go(QStringLiteral("history"));
                 QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("historyRow_") + QString::number(pastId)));
                 QVERIFY((search = findItem(main->contentItem(), QStringLiteral("historySearch"))));
                 search->setProperty("text", QStringLiteral("board"));
             }},
            {QStringLiteral("history recording reader"), [navigation, main, pastId] {
                 navigation->goToRow(QStringLiteral("history"), QStringLiteral("recording:") + QString::number(pastId));
                 QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("deleteEntry")));
             }},
        },
                 main);
    }

    // The session signals only a stream's first lost audio, so Recording
    // reads the count again on its clock. With a result time of 0, the pill
    // goes when the recording stops, with no Saved.
    void aRecordingsLostAudioCountsOnAndAZeroResultTimeSkipsSaved()
    {
        const auto platform = std::make_shared<RecordingPlatform>();
        ApplicationController controller(true, platform);
        forgetRecordings(controller);
        const auto forget = qScopeGuard([&controller] { forgetRecordings(controller); });
        QPointer<test::FakeSpeechTranscriber> stream;
        controller.providerRegistry()->registerSpeechProvider(
            {QStringLiteral("codex"), QStringLiteral("ChatGPT")}, [&stream](QObject *parent) {
                stream = new test::FakeSpeechTranscriber(parent);
                stream->providerId = QStringLiteral("codex");
                stream->streamsFinals = true;
                // Stopping waits this long for the stream that is down.
                stream->utteranceAnswerTimeout = 100;
                return stream.data();
            });
        QTemporaryDir folder;
        controller.settings()->edit([&folder](AppSettings &draft) {
            draft.setupCompleted = true;
            draft.speech.services.order = {QStringLiteral("codex")};
            draft.recording.folder = folder.path();
            draft.recording.systemAudio = false;
            draft.output.completionStatusDurationMs = 0;
            return true;
        });
        controller.settings()->setRecordingConsentNoticeShown(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        RecordingSession *recording = controller.recording();
        QuickRecording quick(&controller);
        quick.start(QString());
        QTRY_VERIFY(stream && stream->startCalls > 0);
        stream->emitConnected();
        QTRY_COMPARE(recording->phase(), RecordingSession::Phase::Recording);
        QQuickWindow *popup = nullptr;
        QTRY_VERIFY((popup = openWindow("PopupWindow")));
        QQuickItem *pill = findItem(popup->contentItem(), QStringLiteral("recordingPill"));
        QVERIFY(pill);

        // Down, with ten minutes waiting and two seconds past them lost.
        stream->emitFailure(QStringLiteral("Connection reset"), true);
        QTRY_COMPARE(recording->status().streams.first().state, RecordingStreamStatus::State::Reconnecting);
        const QByteArray second(32000, '\0');
        for (int i = 0; i < 10 * 60 + 2; ++i) {
            platform->microphone->pushAudio(second);
        }
        const auto lostAudioMs = [&quick] { return quick.streams().first().toMap().value(QStringLiteral("lostAudioMs")).toLongLong(); };
        QTRY_COMPARE(lostAudioMs(), 2000);
        QSignalSpy problems(&quick, &QuickRecording::problemsChanged);
        platform->microphone->pushAudio(second);
        QCOMPARE(recording->status().streams.first().lostAudioMs, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!problems.isEmpty(), 2000);
        QCOMPARE(lostAudioMs(), 3000);

        recording->stop();
        QTRY_COMPARE(recording->phase(), RecordingSession::Phase::Off);
        QVERIFY(!pill->property("saved").toBool());
        QTRY_VERIFY(!pill->property("shown").toBool());
    }

    void everySetupStepLoads_data() { themes(); }
    // Writing profiles and an App's editor link to the vocabulary limited to
    // them, and Vocabulary then lists only that, a correction learned in
    // the App included, with the way back. A limit reads the same way for a
    // profile and an App.
    void profilesAndAppsShowTheVocabularyLimitedToThem()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            draft.apps.apps = {test::idApp(QStringLiteral("org.kde.kate"))};
            VocabularyEntry anywhere{QStringLiteral("Kubernetes")};
            anywhere.id = QStringLiteral("anywhere");
            VocabularyEntry forWork = test::replacementEntry(QStringLiteral("sig"), QStringLiteral("Regards"));
            forWork.id = QStringLiteral("for-work");
            forWork.profiles = {WritingProfile::Work};
            draft.vocabulary = {anywhere, forWork,
                                test::learnedEntry(QStringLiteral("cute"), QStringLiteral("Qt"), QStringLiteral("ORG.KDE.KATE"),
                                                   QStringLiteral("learned-kate"))};
            return true;
        });
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);

        navigation->go(QStringLiteral("profiles"));
        QQuickItem *link = nullptr;
        QTRY_VERIFY((link = findItem(main->contentItem(), QStringLiteral("profileVocabulary"))));
        QVERIFY(link->property("plainText").toString().startsWith(QLatin1String("1 vocabulary entry is limited to this profile.")));
        QMetaObject::invokeMethod(link, "activated");
        QTRY_COMPARE(navigation->current(), QStringLiteral("vocabulary"));
        QCOMPARE(navigation->returnTo(), QStringLiteral("profiles"));
        QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("vocab:for-work")));
        QVERIFY(!findItem(main->contentItem(), QStringLiteral("vocab:anywhere")));
        QVERIFY(!findItem(main->contentItem(), QStringLiteral("vocab:learned-kate")));
        QCOMPARE(findItem(main->contentItem(), QStringLiteral("vocabularyLimitFilter"))->property("currentId").toString(),
                 QStringLiteral("profile/work"));
        QCOMPARE(findItem(findItem(main->contentItem(), QStringLiteral("vocab:for-work")), QStringLiteral("vocabularyLimits"))
                     ->property("text").toString(),
                 QStringLiteral("Only with Work"));

        navigation->goToRow(QStringLiteral("apps"), QStringLiteral("appentry:app:org.kde.kate"));
        QQuickItem *show = nullptr;
        QTRY_VERIFY((show = findItem(main->contentItem(), QStringLiteral("appVocabulary"))));
        QMetaObject::invokeMethod(show, "clicked");
        QTRY_COMPARE(navigation->current(), QStringLiteral("vocabulary"));
        QTRY_VERIFY(findItem(main->contentItem(), QStringLiteral("vocab:learned-kate")));
        // The page filtered to Work slides out after this one comes in.
        QTRY_VERIFY(!findItem(main->contentItem(), QStringLiteral("vocab:for-work")));
        QVERIFY(!findItem(main->contentItem(), QStringLiteral("vocab:anywhere")));
        QCOMPARE(findItem(findItem(main->contentItem(), QStringLiteral("vocab:learned-kate")), QStringLiteral("vocabularyLimits"))
                     ->property("text").toString(),
                 QStringLiteral("Only in org.kde.kate"));
    }

    // One editor takes every kind: an entry a link opens shows in it, and
    // giving a spelling something to write turns it into a replacement.
    void theEditorTurnsASpellingIntoAReplacement()
    {
        ApplicationController controller(true);
        controller.settings()->edit([](AppSettings &draft) {
            VocabularyEntry entry{QStringLiteral("kay eight s")};
            entry.id = QStringLiteral("k8s");
            draft.vocabulary = {entry};
            return true;
        });
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showMainWindow();
        QQuickWindow *main = openWindow("Main");
        QVERIFY(main);
        QVERIFY(QTest::qWaitForWindowExposed(main));
        auto *navigation = qmlEngine(main)->singletonInstance<QuickNavigation *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Navigation"));
        QVERIFY(navigation);

        navigation->goToRow(QStringLiteral("vocabulary"), QStringLiteral("vocab:k8s"));
        QQuickItem *editor = nullptr;
        QTRY_VERIFY((editor = findItem(main->contentItem(), QStringLiteral("vocabularyEditor"))));
        QTRY_VERIFY(editor->property("open").toBool());
        QCOMPARE(findItem(editor, QStringLiteral("vocabularySay"))->property("text").toString(), QStringLiteral("kay eight s"));
        findItem(editor, QStringLiteral("vocabularyWrite"))->setProperty("text", QStringLiteral("K8s"));
        QMetaObject::invokeMethod(editor, "saved");
        QTRY_VERIFY(!editor->property("open").toBool());
        const QList<VocabularyEntry> saved = controller.settings()->snapshot().vocabulary;
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.first().id, QStringLiteral("k8s"));
        QCOMPARE(saved.first().replacement, QStringLiteral("K8s"));
    }

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
    // A first run's Refinement step shows the services in order, with each
    // one's model, thinking and speed folded away until asked for.
    void setupRefinementFoldsEachServicesChoices()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showSetupAssistant(SetupAssistantPage::All);
        QQuickWindow *setup = openWindow("SetupAssistant");
        QVERIFY(setup);
        QVERIFY(QTest::qWaitForWindowExposed(setup));
        const QJSValue steps = setup->property("steps").value<QJSValue>();
        for (int i = 0; i < steps.property(QStringLiteral("length")).toInt(); ++i) {
            if (steps.property(i).property(QStringLiteral("id")).toString() == QLatin1String("refinement")) {
                setup->setProperty("step", i);
            }
        }
        QQuickItem *show = nullptr;
        QTRY_VERIFY((show = findItem(setup->contentItem(), QStringLiteral("showServiceChoices"))));
        QQuickItem *model = nullptr;
        QTRY_VERIFY((model = findItem(setup->contentItem(), QStringLiteral("serviceModel"))));
        QTRY_VERIFY(!model->isVisible());
        QVERIFY(QMetaObject::invokeMethod(show, "clicked"));
        QTRY_VERIFY(model->isVisible());
    }

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
        auto *settings = qmlEngine(setup)->singletonInstance<QuickSettings *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Settings"));
        QVERIFY(settings);
        settings->set(QStringLiteral("keepOnClipboard"), false);
        QVERIFY(controller.settings()->dictationSnapshot().output.steps.keep);
        QVERIFY(QMetaObject::invokeMethod(setup, "finish"));
        QVERIFY(!controller.settings()->dictationSnapshot().setupCompleted);
        QVERIFY(!controller.settings()->dictationSnapshot().output.steps.keep);
    }

    // Setup's Speech to text step is the page's own Service Order, and what
    // it changes waits in setup's draft, which Settings shows to every page:
    // closing setup drops it, and Set up later saves it.
    void setupDraftsWhatItsStepsChange()
    {
        ApplicationController controller(true);
        QuickSettings(&controller).set(QStringLiteral("speechServices"),
                                       test::serviceOrder({QStringLiteral("codex"), QStringLiteral("claude")}));
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        const QStringList saved{QStringLiteral("codex"), QStringLiteral("claude")};
        const QStringList reordered{QStringLiteral("claude"), QStringLiteral("codex")};
        const auto savedOrder = [&controller] { return controller.settings()->dictationSnapshot().speech.services.order; };
        // Opens setup on its Speech to text step and moves Claude first
        // there, as dragging it does.
        const auto reorderInSetup = [&]() -> QQuickWindow * {
            ui.showSetupAssistant(SetupAssistantPage::All);
            QQuickWindow *setup = nullptr;
            [&] { QTRY_VERIFY((setup = openWindow("SetupAssistant"))); }();
            if (!setup) {
                return nullptr;
            }
            const QJSValue steps = setup->property("steps").value<QJSValue>();
            for (int i = 0; i < steps.property(QStringLiteral("length")).toInt(); ++i) {
                if (steps.property(i).property(QStringLiteral("id")).toString() == QLatin1String("transcription")) {
                    setup->setProperty("step", i);
                }
            }
            QQuickItem *order = nullptr;
            [&] { QTRY_VERIFY((order = findItem(setup->contentItem(), QStringLiteral("speechServiceOrder")))); }();
            if (!order || !QMetaObject::invokeMethod(order, "edited", Q_ARG(QVariant, QVariant(reordered)))) {
                return nullptr;
            }
            return setup;
        };

        QQuickWindow *setup = reorderInSetup();
        QVERIFY(setup);
        auto *settings = qmlEngine(setup)->singletonInstance<QuickSettings *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Settings"));
        QVERIFY(settings);
        QCOMPARE(test::servicesOf(settings->values().value(QStringLiteral("speechServices"))), reordered);
        QCOMPARE(savedOrder(), saved);
        setup->close();
        QTRY_VERIFY(!openWindow("SetupAssistant"));
        QCOMPARE(test::servicesOf(settings->values().value(QStringLiteral("speechServices"))), saved);
        QCOMPARE(savedOrder(), saved);

        setup = reorderInSetup();
        QVERIFY(setup);
        QVERIFY(QMetaObject::invokeMethod(setup, "finish"));
        QCOMPARE(savedOrder(), reordered);
        QVERIFY(!controller.settings()->dictationSnapshot().setupCompleted);
    }

    // Ready shows Home's readiness card over setup's draft: a delivery step
    // turned off in setup changes what it says of Pasting at once, as
    // System's outcome does, while the saved step stays on.
    void setupReadyShowsHomesReadinessOverTheDraft()
    {
        ApplicationController controller(true);
        TrayIcon tray(&controller);
        QuickFrontEnd ui(&controller, &tray);
        ui.showSetupAssistant(SetupAssistantPage::All);
        QQuickWindow *setup = openWindow("SetupAssistant");
        QVERIFY(setup);
        QVERIFY(QTest::qWaitForWindowExposed(setup));
        setup->setProperty("step", setup->property("steps").value<QJSValue>().property(QStringLiteral("length")).toInt() - 1);
        QQmlEngine *engine = qmlEngine(setup);
        auto *settings = engine->singletonInstance<QuickSettings *>(QStringLiteral("Speecher.Backend"), QStringLiteral("Settings"));
        auto *system = engine->singletonInstance<QuickSystem *>(QStringLiteral("Speecher.Backend"), QStringLiteral("System"));
        QVERIFY(settings && system);
        QObject *readiness = nullptr;
        QTRY_VERIFY((readiness = findItem(setup->contentItem(), QStringLiteral("readiness"))));
        QVERIFY(readiness->property("inSetup").toBool());
        const auto pastingNeeded = [readiness] { return readiness->property("pastingNeeded").toBool(); };

        QVERIFY(controller.settings()->dictationSnapshot().output.steps.keep);
        settings->set(QStringLiteral("keepOnClipboard"), false);
        QCOMPARE(pastingNeeded(), system->pastingOutcome() == QLatin1String("nothing"));
        settings->set(QStringLiteral("insertThroughAccessibility"), false);
        settings->set(QStringLiteral("pressPasteKeys"), false);
        QCOMPARE(pastingNeeded(), system->pastingOutcome() == QLatin1String("nothing"));
        QVERIFY(controller.settings()->dictationSnapshot().output.steps.keep);
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
