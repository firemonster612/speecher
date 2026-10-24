#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/ColorSchemes.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickAppearance.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QAccessible>
#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>
#include <functional>
#include <memory>

#ifdef SPEECHER_TEST_KWIN_BLUR
#include "platform/KWinBlurBackdrop.h"

#include <QExposeEvent>
#include <QWindow>

#include <cctype>
#include <cstring>
#include <mutex>
#include <sys/socket.h>
#include <thread>
#include <vector>
#include <wayland-client.h>
#include <wayland-server.h>

extern "C" const wl_interface org_kde_kwin_blur_manager_interface;
#endif

using namespace speecher;

namespace {

const QByteArray breezeColors =
    "[ColorEffects:Disabled]\n"
    "Color=56,56,56\n"
    "\n"
    "[Colors:Selection]\n"
    "BackgroundNormal=61,174,233\n"
    "\n"
    "[Colors:View]\n"
    "BackgroundNormal=255,255,255\n"
    "ForegroundNegative=218,68,83\n"
    "\n"
    "[Colors:Window]\n"
    "BackgroundNormal=239,240,241\n"
    "ForegroundNormal=35,38,41\n"
    "\n"
    "[General]\n"
    "ColorScheme=BreezeLight\n"
    "; a comment\n"
    "Name=Breeze Light\n"
    "Name[de]=Breeze Hell\n"
    "\n";

[[nodiscard]] bool writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size() && file.commit();
}

// WCAG 2's contrast ratio, from its definition of relative luminance.
double contrast(const QColor &a, const QColor &b)
{
    const auto channel = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    const auto luminance = [&](const QColor &c) {
        return 0.2126 * channel(c.redF()) + 0.7152 * channel(c.greenF()) + 0.0722 * channel(c.blueF());
    };
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// top, with its alpha, laid over the opaque bottom, as an 8-bit framebuffer
// holds the result.
QColor over(const QColor &top, const QColor &bottom)
{
    const double a = top.alphaF();
    const auto channel = [a](double t, double b) { return qRound((t * a + b * (1 - a)) * 255); };
    return QColor(channel(top.redF(), bottom.redF()), channel(top.greenF(), bottom.greenF()), channel(top.blueF(), bottom.blueF()));
}

#ifdef SPEECHER_TEST_KWIN_BLUR
void acceptAnything(wl_resource *resource);

// Any request to a stand-in's object: the objects it asks for are made, to
// take any request in turn, and a destructor lets the object go.
int acceptRequest(const void *, void *target, uint32_t, const wl_message *message, wl_argument *args)
{
    auto *resource = static_cast<wl_resource *>(target);
    int arg = 0;
    for (const char *type = message->signature; *type; ++type) {
        // The signature's letters are arguments; '?' and versions are not.
        if (!std::isalpha(static_cast<unsigned char>(*type))) {
            continue;
        }
        if (*type == 'n') {
            acceptAnything(wl_resource_create(wl_resource_get_client(resource), message->types[arg],
                                              wl_resource_get_version(resource), args[arg].n));
        }
        ++arg;
    }
    if (std::strcmp(message->name, "destroy") == 0 || std::strcmp(message->name, "release") == 0) {
        wl_resource_destroy(resource);
    }
    return 0;
}

void acceptAnything(wl_resource *resource)
{
    wl_resource_set_dispatcher(resource, acceptRequest, nullptr, nullptr, nullptr);
}

// A global, made with its interface as data, binds to an object that accepts
// anything.
void bindAnything(wl_client *client, void *interface, uint32_t version, uint32_t id)
{
    acceptAnything(wl_resource_create(client, static_cast<const wl_interface *>(interface), int(version), id));
}

// A Wayland compositor offering KWin's blur manager and a wl_compositor to
// make surfaces and regions, which accept whatever is asked of them, on a
// thread of its own. It takes the manager away and offers it again when
// asked, as KWin does when its Blur effect goes off and on.
class BlurCompositor {
public:
    BlurCompositor()
        : m_display(wl_display_create())
    {
        int fds[2];
        if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) == 0) {
            wl_client_create(m_display, fds[0]);
            m_clientFd = fds[1];
        }
        wl_global_create(m_display, &wl_compositor_interface, 4, const_cast<wl_interface *>(&wl_compositor_interface),
                         bindAnything);
        offerManager();
        m_thread = std::thread([this] {
            while (!m_stopped) {
                std::vector<std::function<void()>> asked;
                {
                    const std::lock_guard lock(m_mutex);
                    asked.swap(m_asked);
                }
                for (const auto &ask : asked) {
                    ask();
                }
                wl_event_loop_dispatch(wl_display_get_event_loop(m_display), 5);
                wl_display_flush_clients(m_display);
            }
        });
    }
    ~BlurCompositor()
    {
        m_stopped = true;
        m_thread.join();
        wl_display_destroy(m_display);
    }

    // The client end of the connection, for wl_display_connect_to_fd.
    int clientFd() const { return m_clientFd; }
    void takeManagerAway()
    {
        ask([this] {
            wl_global_destroy(m_manager);
            m_manager = nullptr;
        });
    }
    void offerManagerAgain() { ask([this] { offerManager(); }); }

private:
    void offerManager()
    {
        m_manager = wl_global_create(m_display, &org_kde_kwin_blur_manager_interface, 1,
                                     const_cast<wl_interface *>(&org_kde_kwin_blur_manager_interface), bindAnything);
    }
    void ask(std::function<void()> work)
    {
        const std::lock_guard lock(m_mutex);
        m_asked.push_back(std::move(work));
    }

    wl_display *m_display;
    wl_global *m_manager = nullptr;
    int m_clientFd = -1;
    std::mutex m_mutex;
    std::vector<std::function<void()>> m_asked;
    std::atomic<bool> m_stopped = false;
    std::thread m_thread;
};

// The client's wl_compositor, bound through a registry of the test's own.
wl_compositor *bindCompositor(wl_display *display)
{
    wl_compositor *compositor = nullptr;
    static const wl_registry_listener listener = {
        .global = [](void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t) {
            if (std::strcmp(interface, wl_compositor_interface.name) == 0) {
                *static_cast<wl_compositor **>(data) = static_cast<wl_compositor *>(
                    wl_registry_bind(registry, name, &wl_compositor_interface, 4));
            }
        },
        .global_remove = [](void *, wl_registry *, uint32_t) {},
    };
    wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &listener, &compositor);
    wl_display_roundtrip(display);
    wl_registry_destroy(registry);
    return compositor;
}

// Round trips until changed has count signals, as Qt's dispatch of the
// default queue would deliver them in the app.
[[nodiscard]] bool dispatchUntil(wl_display *display, const QSignalSpy &changed, int count)
{
    for (int i = 0; i < 200 && changed.count() < count; ++i) {
        wl_display_roundtrip(display);
    }
    return changed.count() == count;
}
#endif

} // namespace

// The Appearance settings and what the desktop offers them: its colour
// schemes as core reads them, the Appearance singleton over them, and Theme's
// palette made from any of them, which must stay readable.
class QuickAppearanceTests : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        SettingsStore().raw().clear();
        QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).removeRecursively();
        QFile::remove(kdeglobals());
    }

    void cleanup()
    {
        qunsetenv("XDG_CURRENT_DESKTOP");
        qunsetenv("XDG_CONFIG_DIRS");
    }

    void readsAKdeColorScheme()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("BreezeLight.colors")), breezeColors));

        const ColorScheme scheme = readKdeColorScheme(dir.filePath(QStringLiteral("BreezeLight.colors")));
        QVERIFY(scheme.isValid());
        QCOMPARE(scheme.id, QStringLiteral("BreezeLight"));
        QCOMPARE(scheme.name, QStringLiteral("Breeze Light"));
        QCOMPARE(scheme.window, QColor(239, 240, 241));
        QCOMPARE(scheme.text, QColor(35, 38, 41));
        QCOMPARE(scheme.accent, QColor(61, 174, 233));
        QCOMPARE(scheme.negative, QColor(218, 68, 83));
        QVERIFY(!readKdeColorScheme(dir.filePath(QStringLiteral("missing.colors"))).isValid());
    }

    void kdeglobalsAccentWinsOverTheSelection()
    {
        QTemporaryDir dir;
        QVERIFY(writeFile(dir.filePath(QStringLiteral("kdeglobals")), breezeColors + "AccentColor=#e93d58\n"));
        QCOMPARE(readKdeColorScheme(dir.filePath(QStringLiteral("kdeglobals"))).accent, QColor(0xe9, 0x3d, 0x58));
    }

    void installedSchemesAreByNameAndTheUsersCopyWins()
    {
        QTemporaryDir user;
        QTemporaryDir system;
        QVERIFY(writeFile(colorSchemesFolder(system.path()) + QStringLiteral("/BreezeLight.colors"), breezeColors));
        QVERIFY(writeFile(colorSchemesFolder(user.path()) + QStringLiteral("/BreezeLight.colors"),
                          QByteArray(breezeColors).replace("Name=Breeze Light", "Name=My Breeze")));
        QVERIFY(writeFile(colorSchemesFolder(system.path()) + QStringLiteral("/Amber.colors"),
                          QByteArray(breezeColors).replace("Name=Breeze Light", "Name=Amber")));
        QVERIFY(writeFile(colorSchemesFolder(system.path()) + QStringLiteral("/Broken.colors"), "[General]\nName=Broken\n"));

        const QList<ColorScheme> schemes = installedColorSchemes({user.path(), system.path()});
        QCOMPARE(schemes.size(), 2);
        QCOMPARE(schemes.at(0).name, QStringLiteral("Amber"));
        QCOMPARE(schemes.at(1).name, QStringLiteral("My Breeze"));
    }

    void settingsSaveThroughTheStore()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QCOMPARE(settings.values().value(QStringLiteral("colorScheme")).toString(), QStringLiteral("steno"));
        QCOMPARE(settings.values().value(QStringLiteral("translucency")).toBool(), true);
        QCOMPARE(settings.values().value(QStringLiteral("windowLayout")).toString(), QStringLiteral("framed"));

        settings.set(QStringLiteral("colorScheme"), QStringLiteral("installed:BreezeDark"));
        settings.set(QStringLiteral("translucency"), false);
        settings.set(QStringLiteral("windowLayout"), QStringLiteral("split"));

        SettingsStore another;
        QCOMPARE(another.colorScheme(), QStringLiteral("installed:BreezeDark"));
        QCOMPARE(another.translucency(), false);
        QCOMPARE(another.windowLayout(), QStringLiteral("split"));
        another.setWindowLayout(QStringLiteral("sideways"));
        QCOMPARE(another.windowLayout(), QStringLiteral("framed"));
    }

    void systemColorsFollowPlasmasKdeglobals()
    {
        qputenv("XDG_CURRENT_DESKTOP", "KDE");
        QVERIFY(writeFile(kdeglobals(), breezeColors));
        // Ahead of the system's own Breeze Light, where it has one.
        QVERIFY(writeFile(colorSchemesFolder(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                              + QStringLiteral("/BreezeLight.colors"),
                          QByteArray(breezeColors).replace("Name=Breeze Light", "Name=Test Breeze")));
        ApplicationController controller(true);
        QuickAppearance appearance(&controller);
        QCOMPARE(appearance.systemColors().value(QStringLiteral("window")).value<QColor>(), QColor(239, 240, 241));
        QStringList breezes;
        for (const QVariant &scheme : appearance.installedSchemes()) {
            if (scheme.toMap().value(QStringLiteral("id")) == QStringLiteral("BreezeLight")) {
                breezes.append(scheme.toMap().value(QStringLiteral("name")).toString());
            }
        }
        QCOMPARE(breezes, QStringList{QStringLiteral("Test Breeze")});

        // As Plasma saves a new scheme: by replacing the file.
        QSignalSpy changed(&appearance, &QuickAppearance::systemColorsChanged);
        QVERIFY(writeFile(kdeglobals(), QByteArray(breezeColors).replace("239,240,241", "32,35,38")));
        QVERIFY(changed.wait(5000));
        QCOMPARE(appearance.systemColors().value(QStringLiteral("window")).value<QColor>(), QColor(32, 35, 38));
    }

    // A kdeglobals made after the start, and then a user's copy ahead of the
    // system's, are each followed.
    void systemColorsFollowTheKdeglobalsInEffect()
    {
        qputenv("XDG_CURRENT_DESKTOP", "KDE");
        QTemporaryDir system;
        qputenv("XDG_CONFIG_DIRS", system.path().toUtf8());
        QVERIFY(QDir().mkpath(QFileInfo(kdeglobals()).absolutePath()));
        ApplicationController controller(true);
        QuickAppearance appearance(&controller);
        QVERIFY(appearance.systemColors().isEmpty());

        QSignalSpy changed(&appearance, &QuickAppearance::systemColorsChanged);
        QVERIFY(writeFile(system.filePath(QStringLiteral("kdeglobals")), QByteArray(breezeColors).replace("239,240,241", "32,35,38")));
        QVERIFY(changed.wait(5000));
        QCOMPARE(appearance.systemColors().value(QStringLiteral("window")).value<QColor>(), QColor(32, 35, 38));

        QVERIFY(writeFile(kdeglobals(), breezeColors));
        QVERIFY(changed.wait(5000));
        QCOMPARE(appearance.systemColors().value(QStringLiteral("window")).value<QColor>(), QColor(239, 240, 241));
    }

    // Off Plasma the desktop's colours are the platform palette's accent,
    // which the UI reads for itself.
    void offPlasmaKdeglobalsIsIgnored()
    {
        qputenv("XDG_CURRENT_DESKTOP", "GNOME");
        QVERIFY(writeFile(kdeglobals(), breezeColors));
        ApplicationController controller(true);
        QuickAppearance appearance(&controller);
        QVERIFY(appearance.systemColors().isEmpty());
    }

    // Every preset, and desktop schemes from tame to garish, one with a
    // window too pale for its white text, in light and dark, opaque and
    // translucent: each ink reads on every ground it is laid on. Text at
    // AAA, secondary text at AA and tertiary at 3:1 on the page, the cards
    // and the sidebar; the accent at AA on the page and cards; failures at AA
    // wherever they show; ink on the accent at AA. Translucent, the sidebar
    // is its paper over anything from a black to a white desktop.
    void everySchemeKeepsTextReadable()
    {
        QQmlEngine engine;
        auto *theme = engine.singletonInstance<QObject *>("Speecher.Theme", "Theme");
        QVERIFY(theme);
        QJSValueList sources;
        const QJSValue presets = theme->property("presets").value<QJSValue>();
        for (int i = 0; i < presets.property(QStringLiteral("length")).toInt(); ++i) {
            sources.append(presets.property(i));
        }
        const auto roles = [&engine](const char *json) { return engine.evaluate(QStringLiteral("(%1)").arg(QLatin1String(json))); };
        sources.append(roles("{ window: '#EFF0F1', text: '#232629', accent: '#3DAEE9', negative: '#DA4453' }"));
        sources.append(roles("{ window: '#202326', text: '#FCFCFC', accent: '#3DAEE9', negative: '#DA4453' }"));
        sources.append(roles("{ window: '#FFFF80', text: '#808000', accent: '#FFFF00', negative: '#FF8080' }"));
        sources.append(roles("{ window: '#000080', text: '#0000FF', accent: '#000040' }"));
        sources.append(roles("{ window: '#707070', text: '#FFFFFF' }"));
        sources.append(roles("{ accent: '#E95420' }"));
        sources.append(roles("{}"));

        QString failures;
        for (const QJSValue &source : sources) {
            for (const bool dark : {false, true}) {
                for (const bool translucent : {false, true}) {
                    theme->setProperty("colorSource", QVariant::fromValue(source));
                    theme->setProperty("dark", dark);
                    theme->setProperty("translucent", translucent);
                    const auto color = [theme](const char *name) { return theme->property(name).value<QColor>(); };
                    const QString name = source.hasProperty(QStringLiteral("id"))
                                             ? source.property(QStringLiteral("id")).toString()
                                             : engine.globalObject().property(QStringLiteral("JSON")).property(QStringLiteral("stringify"))
                                                   .call({source}).toString();
                    const QString label = QStringLiteral("\n%1%2%3").arg(name, dark ? " dark" : " light", translucent ? " translucent" : "");
                    const QColor side = color("sidePaper");
                    QCOMPARE(side.alpha() < 255, translucent);
                    const QList<QColor> page{color("sheet"), color("raised")};
                    const QList<QColor> sidebar = translucent ? QList<QColor>{over(side, Qt::black), over(side, Qt::white)}
                                                              : QList<QColor>{side};
                    const auto unreadable = [&](const char *ink, const QList<QColor> &grounds, double ratio) {
                        QString found;
                        for (const QColor &ground : grounds) {
                            const double ratioOn = contrast(color(ink), ground);
                            if (ratioOn < ratio) {
                                found += QStringLiteral("\n  %1 on %2: %3").arg(QLatin1String(ink), ground.name()).arg(ratioOn, 0, 'f', 4);
                            }
                        }
                        return found;
                    };
                    const QString unread = unreadable("text", page + sidebar, 7) + unreadable("textSecondary", page + sidebar, 4.5)
                                           + unreadable("textTertiary", page + sidebar, 3) + unreadable("accent", page, 4.5)
                                           + unreadable("negative", page + sidebar, 4.5)
                                           + unreadable("accentText", {color("accent")}, 4.5);
                    if (!unread.isEmpty()) {
                        failures += label + unread;
                    }
                }
            }
        }
        QVERIFY2(failures.isEmpty(), qPrintable(failures));
    }

#ifdef SPEECHER_TEST_KWIN_BLUR
    // KWin offers its blur manager only while its Blur effect is on: the
    // backdrop is available while it is offered, and says so as it goes and
    // comes back.
    void kwinBlurFollowsTheBlurEffect()
    {
        BlurCompositor compositor;
        wl_display *display = wl_display_connect_to_fd(compositor.clientFd());
        QVERIFY(display);
        {
            KWinBlurBackdrop backdrop(display, nullptr, {});
            QVERIFY(backdrop.available());
            QSignalSpy changed(&backdrop, &WindowBackdrop::availableChanged);

            compositor.takeManagerAway();
            QVERIFY(dispatchUntil(display, changed, 1));
            QVERIFY(!backdrop.available());

            compositor.offerManagerAgain();
            QVERIFY(dispatchUntil(display, changed, 2));
            QVERIFY(backdrop.available());
        }
        wl_display_disconnect(display);
    }

    // A blurred window shown again after KWin took the manager away (Blur
    // turned off, then the window minimised and restored) asks nothing of
    // the manager that is gone.
    void kwinBlurLeavesAWindowAloneOnceTheManagerGoes()
    {
        BlurCompositor compositor;
        wl_display *display = wl_display_connect_to_fd(compositor.clientFd());
        QVERIFY(display);
        wl_compositor *surfaces = bindCompositor(display);
        QVERIFY(surfaces);
        wl_surface *surface = wl_compositor_create_surface(surfaces);
        QWindow window;
        window.resize(200, 100);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        {
            KWinBlurBackdrop backdrop(display, surfaces, [surface](QWindow *) { return surface; });
            QSignalSpy changed(&backdrop, &WindowBackdrop::availableChanged);
            backdrop.set(&window, QRect(0, 0, 200, 100), 8, false);

            compositor.takeManagerAway();
            QVERIFY(dispatchUntil(display, changed, 1));
            QExposeEvent expose(QRegion(0, 0, 200, 100));
            QCoreApplication::sendEvent(&window, &expose);
            QVERIFY(wl_display_roundtrip(display) >= 0);
        }
        wl_surface_destroy(surface);
        wl_compositor_destroy(surfaces);
        wl_display_disconnect(display);
    }
#endif

    // To assistive technology the pictures are radio buttons, named, the
    // chosen one checked, each chosen by its Press or Toggle action. From
    // the keyboard, arrows move the choice, and Return or Space choose the
    // first picture when the choice is in another picker.
    void lookPickerIsARadioGroup()
    {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        QAccessible::setActive(true);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import "app"
            Window {
                id: root
                property string chosen
                property alias picker: picker
                width: 600; height: 300; visible: true
                LookPicker {
                    id: picker
                    width: 420
                    focus: true
                    options: [{ id: "ash", label: "Ash" }, { id: "birch", label: "Birch" }, { id: "cedar", label: "Cedar" }]
                    currentId: root.chosen
                    thumbnail: Rectangle { property var option }
                    onActivated: id => root.chosen = id
                }
            })",
                          QUrl(QStringLiteral("qrc:/qt/qml/Speecher/App/pages/LookPickerProbe.qml")));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(QTest::qWaitForWindowActive(window));
        auto *picker = root->property("picker").value<QQuickItem *>();

        QAccessibleInterface *list = QAccessible::queryAccessibleInterface(picker);
        QVERIFY(list);
        QCOMPARE(list->role(), QAccessible::List);
        QList<QAccessibleInterface *> buttons;
        for (int i = 0; i < list->childCount(); ++i) {
            if (list->child(i)->role() == QAccessible::RadioButton) {
                buttons.append(list->child(i));
            }
        }
        const auto names = [&buttons] {
            QStringList named;
            for (QAccessibleInterface *button : buttons) {
                named.append(button->text(QAccessible::Name) + (button->state().checked ? "*" : ""));
            }
            return named;
        };
        QCOMPARE(names(), (QStringList{"Ash", "Birch", "Cedar"}));

        buttons.at(1)->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(root->property("chosen").toString(), QStringLiteral("birch"));
        QCOMPARE(names(), (QStringList{"Ash", "Birch*", "Cedar"}));
        buttons.at(2)->actionInterface()->doAction(QAccessibleActionInterface::toggleAction());
        QCOMPARE(root->property("chosen").toString(), QStringLiteral("cedar"));

        for (const Qt::Key key : {Qt::Key_Return, Qt::Key_Space}) {
            root->setProperty("chosen", QString());
            picker->forceActiveFocus();
            QTest::keyClick(window, key);
            QCOMPARE(root->property("chosen").toString(), QStringLiteral("ash"));
        }
        QTest::keyClick(window, Qt::Key_Right);
        QCOMPARE(root->property("chosen").toString(), QStringLiteral("birch"));
        QTest::keyClick(window, Qt::Key_Space);
        QCOMPARE(root->property("chosen").toString(), QStringLiteral("birch"));
    }

    // Where nothing blurs, search shows the translucency switch as General
    // does: off and greyed, the saved choice kept for when blur comes back.
    void searchShowsTranslucencyAsGeneralDoes()
    {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickAppearance appearance(&controller);
        QVERIFY(!appearance.canBlur());
        QCOMPARE(settings.values().value(QStringLiteral("translucency")).toBool(), true);
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Speecher.App
            Window {
                width: 700; height: 500; visible: true
                SearchPalette { anchors.fill: parent; shown: true; query: "translucent sidebar" }
            })",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(root.get());
        QVERIFY(QTest::qWaitForWindowExposed(window));

        const std::function<QList<QQuickItem *>(QQuickItem *)> shownSwitches = [&](QQuickItem *item) {
            QList<QQuickItem *> found;
            if (!item->isVisible()) {
                return found;
            }
            if (QLatin1String(item->metaObject()->className()).startsWith(QLatin1String("Switch_"))) {
                found.append(item);
            }
            for (QQuickItem *child : item->childItems()) {
                found += shownSwitches(child);
            }
            return found;
        };
        QTRY_COMPARE(shownSwitches(window->contentItem()).size(), 1);
        QQuickItem *toggle = shownSwitches(window->contentItem()).first();
        QVERIFY(!toggle->property("checked").toBool());
        QVERIFY(!toggle->isEnabled());
        QCOMPARE(settings.values().value(QStringLiteral("translucency")).toBool(), true);
    }

private:
    static QString kdeglobals()
    {
        return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QStringLiteral("/kdeglobals");
    }
};

int runQuickAppearanceTests(int argc, char **argv)
{
    QuickAppearanceTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_appearance.moc"
