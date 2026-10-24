#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/ColorSchemes.h"
#include "core/SettingsStore.h"
#include "frontend/quick/host/QuickAppearance.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QDir>
#include <QFile>
#include <QJSValue>
#include <QQmlEngine>
#include <QSaveFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cmath>
#include <memory>

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

void writeFile(const QString &path, const QByteArray &contents)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
    QVERIFY(file.commit());
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

    void readsAKdeColorScheme()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("BreezeLight.colors")), breezeColors);

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
        writeFile(dir.filePath(QStringLiteral("kdeglobals")), breezeColors + "AccentColor=#e93d58\n");
        QCOMPARE(readKdeColorScheme(dir.filePath(QStringLiteral("kdeglobals"))).accent, QColor(0xe9, 0x3d, 0x58));
    }

    void installedSchemesAreByNameAndTheUsersCopyWins()
    {
        QTemporaryDir user;
        QTemporaryDir system;
        writeFile(colorSchemesFolder(system.path()) + QStringLiteral("/BreezeLight.colors"), breezeColors);
        writeFile(colorSchemesFolder(user.path()) + QStringLiteral("/BreezeLight.colors"),
                  QByteArray(breezeColors).replace("Name=Breeze Light", "Name=My Breeze"));
        writeFile(colorSchemesFolder(system.path()) + QStringLiteral("/Amber.colors"),
                  QByteArray(breezeColors).replace("Name=Breeze Light", "Name=Amber"));
        writeFile(colorSchemesFolder(system.path()) + QStringLiteral("/Broken.colors"), "[General]\nName=Broken\n");

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
        writeFile(kdeglobals(), breezeColors);
        // Ahead of the system's own Breeze Light, where it has one.
        writeFile(colorSchemesFolder(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                      + QStringLiteral("/BreezeLight.colors"),
                  QByteArray(breezeColors).replace("Name=Breeze Light", "Name=Test Breeze"));
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
        writeFile(kdeglobals(), QByteArray(breezeColors).replace("239,240,241", "32,35,38"));
        QVERIFY(changed.wait(5000));
        QCOMPARE(appearance.systemColors().value(QStringLiteral("window")).value<QColor>(), QColor(32, 35, 38));
        qunsetenv("XDG_CURRENT_DESKTOP");
    }

    void offPlasmaSystemColorsAreThePlatformAccent()
    {
        qputenv("XDG_CURRENT_DESKTOP", "GNOME");
        writeFile(kdeglobals(), breezeColors);
        ApplicationController controller(true);
        QuickAppearance appearance(&controller);
        QCOMPARE(appearance.systemColors().keys(), QStringList{QStringLiteral("accent")});
        qunsetenv("XDG_CURRENT_DESKTOP");
    }

    // Every preset, and desktop schemes from tame to garish, in light and
    // dark: text reads at AAA on the page and cards, quieter text and the
    // accent at AA, failures at the 3:1 of a UI mark, and ink on the accent
    // at AA.
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
        sources.append(roles("{ accent: '#E95420' }"));
        sources.append(roles("{}"));

        for (const QJSValue &source : sources) {
            for (const bool dark : {false, true}) {
                theme->setProperty("colorSource", QVariant::fromValue(source));
                theme->setProperty("dark", dark);
                const auto color = [theme](const char *name) { return theme->property(name).value<QColor>(); };
                const QString label = engine.globalObject().property(QStringLiteral("JSON")).property(QStringLiteral("stringify"))
                                          .call({source}).toString()
                                      + (dark ? " dark" : " light");
                for (const char *surface : {"sheet", "raised"}) {
                    QVERIFY2(contrast(color("text"), color(surface)) >= 7, qPrintable(label + " text on " + surface));
                    QVERIFY2(contrast(color("textSecondary"), color(surface)) >= 4.5,
                             qPrintable(label + " secondary text on " + surface));
                    QVERIFY2(contrast(color("accent"), color(surface)) >= 4.5, qPrintable(label + " accent on " + surface));
                    QVERIFY2(contrast(color("negative"), color(surface)) >= 3, qPrintable(label + " negative on " + surface));
                }
                QVERIFY2(contrast(color("accentText"), color("accent")) >= 4.5, qPrintable(label + " ink on accent"));
            }
        }
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
