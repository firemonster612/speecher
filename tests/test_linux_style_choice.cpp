#include "common/test_suites.h"
#include "platform/LinuxStyleChoice.h"

using namespace speecher;

class LinuxStyleChoiceTests : public QObject {
    Q_OBJECT

private slots:
    void choosesHostStyle_data()
    {
        QTest::addColumn<QString>("overrideStyle");
        QTest::addColumn<QString>("kdeWidgetStyle");
        QTest::addColumn<QString>("desktop");
        QTest::addColumn<QString>("platformTheme");
        QTest::addColumn<QString>("currentStyle");
        QTest::addColumn<QStringList>("availableStyles");
        QTest::addColumn<QString>("applicationTheme");
        QTest::addColumn<bool>("desktopPrefersDark");
        QTest::addColumn<QString>("requested");
        QTest::addColumn<QString>("chosen");

        QTest::newRow("override")
            << QStringLiteral("Windows") << QStringLiteral("Breeze")
            << QStringLiteral("KDE") << QStringLiteral("kde") << QStringLiteral("Breeze")
            << QStringList{QStringLiteral("Breeze"), QStringLiteral("Fusion"), QStringLiteral("Windows")}
            << QStringLiteral("system") << false
            << QStringLiteral("Windows") << QStringLiteral("Windows");
        QTest::newRow("unavailable override")
            << QStringLiteral("Oxygen") << QStringLiteral("Breeze")
            << QStringLiteral("KDE") << QStringLiteral("kde") << QStringLiteral("Breeze")
            << QStringList{QStringLiteral("Breeze"), QStringLiteral("Fusion")}
            << QStringLiteral("system") << false
            << QStringLiteral("Oxygen") << QStringLiteral("Breeze");
        QTest::newRow("unavailable override keeps current non-GTK style")
            << QStringLiteral("Oxygen") << QString() << QStringLiteral("LXQt")
            << QStringLiteral("lxqt") << QStringLiteral("kvantum")
            << QStringList{QStringLiteral("Fusion"), QStringLiteral("kvantum")}
            << QStringLiteral("system") << false
            << QStringLiteral("Oxygen") << QStringLiteral("kvantum");
        QTest::newRow("KDE widget style")
            << QString() << QStringLiteral("kvantum") << QStringLiteral("KDE")
            << QStringLiteral("kde") << QStringLiteral("Breeze")
            << QStringList{QStringLiteral("Breeze"), QStringLiteral("Fusion"), QStringLiteral("kvantum")}
            << QStringLiteral("system") << false
            << QStringLiteral("kvantum") << QStringLiteral("kvantum");
        QTest::newRow("missing KDE style")
            << QString() << QStringLiteral("Oxygen") << QStringLiteral("KDE")
            << QStringLiteral("kde") << QStringLiteral("Oxygen")
            << QStringList{QStringLiteral("Breeze"), QStringLiteral("Fusion")}
            << QStringLiteral("system") << false
            << QStringLiteral("Oxygen") << QStringLiteral("Breeze");
        QTest::newRow("KDE without widget style")
            << QString() << QString() << QStringLiteral("KDE")
            << QStringLiteral("kde") << QStringLiteral("Fusion")
            << QStringList{QStringLiteral("Breeze"), QStringLiteral("Fusion")}
            << QStringLiteral("system") << false
            << QString() << QStringLiteral("Breeze");
        QTest::newRow("GNOME system dark")
            << QString() << QString() << QStringLiteral("ubuntu:GNOME")
            << QStringLiteral("gtk3") << QStringLiteral("Fusion")
            << QStringList{QStringLiteral("Adwaita"), QStringLiteral("Adwaita-Dark"), QStringLiteral("Fusion")}
            << QStringLiteral("system") << true
            << QStringLiteral("adwaita-dark") << QStringLiteral("Adwaita-Dark");
        QTest::newRow("GNOME portal platform theme")
            << QString() << QString() << QStringLiteral("ubuntu:GNOME")
            << QStringLiteral("xdgdesktopportal") << QStringLiteral("Fusion")
            << QStringList{QStringLiteral("Adwaita"), QStringLiteral("Adwaita-Dark"), QStringLiteral("Fusion")}
            << QStringLiteral("system") << false
            << QStringLiteral("adwaita") << QStringLiteral("Adwaita");
        QTest::newRow("Speecher light overrides desktop dark")
            << QString() << QString() << QStringLiteral("GNOME")
            << QStringLiteral("gtk3") << QStringLiteral("Adwaita-Dark")
            << QStringList{QStringLiteral("Adwaita"), QStringLiteral("Adwaita-Dark"), QStringLiteral("Fusion")}
            << QStringLiteral("light") << true
            << QStringLiteral("adwaita") << QStringLiteral("Adwaita");
        QTest::newRow("Speecher dark overrides desktop light")
            << QString() << QString() << QStringLiteral("GNOME")
            << QStringLiteral("gtk3") << QStringLiteral("Adwaita")
            << QStringList{QStringLiteral("Adwaita"), QStringLiteral("Adwaita-Dark"), QStringLiteral("Fusion")}
            << QStringLiteral("dark") << false
            << QStringLiteral("adwaita-dark") << QStringLiteral("Adwaita-Dark");
        QTest::newRow("GTK platform theme on unlisted desktop")
            << QString() << QString() << QStringLiteral("sway")
            << QStringLiteral("gtk3") << QStringLiteral("Fusion")
            << QStringList{QStringLiteral("Adwaita"), QStringLiteral("Adwaita-Dark"), QStringLiteral("Fusion")}
            << QStringLiteral("system") << false
            << QStringLiteral("adwaita") << QStringLiteral("Adwaita");
        QTest::newRow("Pantheon desktop fallback")
            << QString() << QString() << QStringLiteral("Pantheon")
            << QString() << QStringLiteral("Fusion")
            << QStringList{QStringLiteral("Adwaita"), QStringLiteral("Fusion")}
            << QStringLiteral("system") << false
            << QStringLiteral("adwaita") << QStringLiteral("Adwaita");
        QTest::newRow("GNOME without Adwaita")
            << QString() << QString() << QStringLiteral("GNOME")
            << QStringLiteral("gtk3") << QStringLiteral("Fusion")
            << QStringList{QStringLiteral("Fusion")}
            << QStringLiteral("system") << false
            << QStringLiteral("adwaita") << QStringLiteral("Fusion");
        QTest::newRow("unknown desktop keeps current style")
            << QString() << QString() << QStringLiteral("sway")
            << QString() << QStringLiteral("Windows")
            << QStringList{QStringLiteral("Fusion"), QStringLiteral("Windows")}
            << QStringLiteral("system") << true
            << QString() << QStringLiteral("Windows");
        QTest::newRow("non-KDE desktop keeps current Kvantum style")
            << QString() << QString() << QStringLiteral("LXQt")
            << QStringLiteral("lxqt") << QStringLiteral("kvantum")
            << QStringList{QStringLiteral("Fusion"), QStringLiteral("kvantum")}
            << QStringLiteral("system") << false
            << QString() << QStringLiteral("kvantum");
        QTest::newRow("KDE platform theme on unlisted desktop")
            << QString() << QString() << QStringLiteral("sway")
            << QStringLiteral("kde") << QStringLiteral("Breeze")
            << QStringList{QStringLiteral("Breeze"), QStringLiteral("Fusion")}
            << QStringLiteral("system") << false
            << QString() << QStringLiteral("Breeze");
    }

    void choosesHostStyle()
    {
        QFETCH(QString, overrideStyle);
        QFETCH(QString, kdeWidgetStyle);
        QFETCH(QString, desktop);
        QFETCH(QString, platformTheme);
        QFETCH(QString, currentStyle);
        QFETCH(QStringList, availableStyles);
        QFETCH(QString, applicationTheme);
        QFETCH(bool, desktopPrefersDark);
        QFETCH(QString, requested);
        QFETCH(QString, chosen);

        const LinuxStyleChoice choice = chooseLinuxStyle(
            overrideStyle,
            kdeWidgetStyle,
            desktop,
            platformTheme,
            currentStyle,
            availableStyles,
            applicationTheme,
            desktopPrefersDark);

        QCOMPARE(choice.requested, requested);
        QCOMPARE(choice.chosen, chosen);
    }
};

int runLinuxStyleChoiceTests(int argc, char **argv)
{
    LinuxStyleChoiceTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_linux_style_choice.moc"
