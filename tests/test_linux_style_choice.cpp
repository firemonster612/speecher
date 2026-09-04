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
        QTest::addColumn<QStringList>("availableStyles");
        QTest::addColumn<bool>("prefersDark");
        QTest::addColumn<QString>("requested");
        QTest::addColumn<QString>("chosen");

        QTest::newRow("override")
            << QStringLiteral("Windows") << QStringLiteral("Breeze")
            << QStringLiteral("KDE")
            << QStringList{QStringLiteral("Breeze"), QStringLiteral("Fusion"), QStringLiteral("Windows")}
            << false << QStringLiteral("Windows") << QStringLiteral("Windows");
        QTest::newRow("KDE widget style")
            << QString() << QStringLiteral("kvantum") << QStringLiteral("KDE")
            << QStringList{QStringLiteral("Breeze"), QStringLiteral("Fusion"), QStringLiteral("kvantum")}
            << false << QStringLiteral("kvantum") << QStringLiteral("kvantum");
        QTest::newRow("missing KDE style")
            << QString() << QStringLiteral("Oxygen") << QStringLiteral("KDE")
            << QStringList{QStringLiteral("Breeze"), QStringLiteral("Fusion")}
            << false << QStringLiteral("Oxygen") << QStringLiteral("Breeze");
        QTest::newRow("GNOME dark")
            << QString() << QString() << QStringLiteral("ubuntu:GNOME")
            << QStringList{QStringLiteral("Adwaita"), QStringLiteral("Adwaita-Dark"), QStringLiteral("Fusion")}
            << true << QStringLiteral("adwaita-dark") << QStringLiteral("Adwaita-Dark");
        QTest::newRow("GNOME without Adwaita")
            << QString() << QString() << QStringLiteral("GNOME")
            << QStringList{QStringLiteral("Fusion")}
            << false << QStringLiteral("adwaita") << QStringLiteral("Fusion");
        QTest::newRow("other desktop")
            << QString() << QString() << QStringLiteral("LXQt")
            << QStringList{QStringLiteral("Fusion")}
            << true << QStringLiteral("Fusion") << QStringLiteral("Fusion");
    }

    void choosesHostStyle()
    {
        QFETCH(QString, overrideStyle);
        QFETCH(QString, kdeWidgetStyle);
        QFETCH(QString, desktop);
        QFETCH(QStringList, availableStyles);
        QFETCH(bool, prefersDark);
        QFETCH(QString, requested);
        QFETCH(QString, chosen);

        const LinuxStyleChoice choice = chooseLinuxStyle(
            overrideStyle, kdeWidgetStyle, desktop, availableStyles, prefersDark);

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
