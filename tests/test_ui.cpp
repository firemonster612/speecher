#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"
#include "ui/AccessibilityNotice.h"
#include "ui/settings/ApplicationSettingsPage.h"
#include "ui/settings/CorrectionsSettingsPage.h"
#include "ui/settings/OutputSettingsPage.h"
#include "ui/settings/RefinementSettingsPage.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QFontMetrics>
#include <QPushButton>
#include <QStyleHints>
#include <QTableWidget>

using namespace speecher::test;


class UiTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    void wordPreview()
    {
        QCOMPARE(WordPreview::lastWords(QStringLiteral(" one  two, three\nfour "), 2), QStringLiteral("three four"));
        QCOMPARE(WordPreview::lastWords(QStringLiteral("short"), 8), QStringLiteral("short"));
        QCOMPARE(WordPreview::lastWords(QString(), 8), QString());
        QCOMPARE(WordPreview::lastWords(QStringLiteral("alpha beta gamma"), 1), QStringLiteral("gamma"));
        QCOMPARE(WordPreview::lastWords(QStringLiteral("alpha beta gamma"), 0), QString());
    }

    void themeUsesThePlatformColorSchemeHint()
    {
        Theme::apply(QStringLiteral("dark"));
        if (qApp->styleHints()->colorScheme() != Qt::ColorScheme::Dark) {
            QSKIP("Platform theme does not honor color-scheme overrides");
        }
        Theme::apply(QStringLiteral("light"));
        QCOMPARE(qApp->styleHints()->colorScheme(), Qt::ColorScheme::Light);
        Theme::apply(QStringLiteral("system"));
        QCOMPARE(qApp->styleHints()->colorScheme(), Qt::ColorScheme::Unknown);
    }

    void accessibilityNoticeExplainsAndOffersTheFix()
    {
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *notice = new AccessibilityNotice(&window);
        layout->addWidget(notice);
        window.show();

        notice->setState(false, false);
        QVERIFY(notice->isVisible());
        auto *message = notice->findChild<QLabel *>(QStringLiteral("accessibilityNoticeMessage"));
        auto *button = notice->findChild<QPushButton *>(QStringLiteral("enableAccessibilityButton"));
        QVERIFY(message);
        QVERIFY(button);
        QVERIFY(message->text().contains(QStringLiteral("AT-SPI")));
        QCOMPARE(button->text(), QStringLiteral("Enable permanently"));
        QSignalSpy requested(notice, &AccessibilityNotice::enableRequested);
        button->click();
        QCOMPARE(requested.count(), 1);

        notice->setState(true, false);
        QVERIFY(notice->isVisible());
        QVERIFY(message->text().contains(QStringLiteral("only for this session")));

        notice->setState(true, true);
        QVERIFY(!notice->isVisible());
    }

    void targetAwareSettingsDisableWithoutAtSpi()
    {
        SettingsStore settings;
        ProviderRegistry providers;
        OutputSettingsPage output(settings);
        ApplicationSettingsPage applications;
        RefinementSettingsPage refinement(providers);
        CorrectionsSettingsPage corrections;
        auto *correctionLearning = corrections.findChild<QCheckBox *>(
            QStringLiteral("correctionLearningControl"));
        QVERIFY(correctionLearning);
        QVERIFY(correctionLearning->toolTip().contains(QStringLiteral("repeated")));
        QVERIFY(!correctionLearning->toolTip().contains(QStringLiteral("only high-confidence")));

        output.setTargetAccessibilityAvailable(false);
        applications.setTargetAccessibilityAvailable(false);
        refinement.setTargetAccessibilityAvailable(false);
        corrections.setTargetAccessibilityAvailable(false);

        QVERIFY(!output.findChild<QWidget *>(QStringLiteral("targetPasteControls"))->isEnabled());
        QVERIFY(!applications.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"))->isEnabled());
        QVERIFY(!refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(!corrections.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());

        output.setTargetAccessibilityAvailable(true);
        applications.setTargetAccessibilityAvailable(true);
        refinement.setTargetAccessibilityAvailable(true);
        corrections.setTargetAccessibilityAvailable(true);
        QVERIFY(correctionLearning->toolTip().contains(QStringLiteral("repeated")));
        QVERIFY(!correctionLearning->toolTip().contains(QStringLiteral("only high-confidence")));
        QVERIFY(output.findChild<QWidget *>(QStringLiteral("targetPasteControls"))->isEnabled());
        QVERIFY(applications.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"))->isEnabled());
        QVERIFY(refinement.findChild<QWidget *>(QStringLiteral("targetContextControl"))->isEnabled());
        QVERIFY(corrections.findChild<QWidget *>(QStringLiteral("correctionLearningControl"))->isEnabled());
    }

    void applicationSettingsShowsBuiltInsAndAddsCustomRules()
    {
        ApplicationSettingsPage page;
        AppSettings settings;
        settings.refinement.writingProfileOverrides = {
            {QStringLiteral("org.legacy.chat"), WritingProfile::Personal, true},
        };
        page.load(settings);

        auto *table = page.findChild<QTableWidget *>(QStringLiteral("appRecognitionRules"));
        auto *add = page.findChild<QPushButton *>(QStringLiteral("addAppRecognitionRule"));
        QVERIFY(table);
        QVERIFY(add);
        QCOMPARE(table->rowCount(), builtInAppRecognitionRules().size() + 1);

        add->click();
        QCOMPARE(table->rowCount(), builtInAppRecognitionRules().size() + 2);
        const int row = table->rowCount() - 1;
        table->item(row, 0)->setText(QStringLiteral("com.acme.shell"));
        auto *category = qobject_cast<QComboBox *>(table->cellWidget(row, 1));
        auto *profile = qobject_cast<QComboBox *>(table->cellWidget(row, 2));
        QVERIFY(category);
        QVERIFY(profile);
        category->setCurrentIndex(category->findData(QStringLiteral("terminal")));
        profile->setCurrentIndex(profile->findData(QStringLiteral("work")));

        page.appendToDraft(settings);
        QCOMPARE(settings.appRecognitionRules.size(), 2);
        QVERIFY(settings.refinement.writingProfileOverrides.isEmpty());
        QCOMPARE(settings.appRecognitionRules.last().match, QStringLiteral("com.acme.shell"));
        QCOMPARE(settings.appRecognitionRules.last().category, AppCategory::Terminal);
        QCOMPARE(settings.appRecognitionRules.last().writingProfile, WritingProfile::Work);
    }

    void sharedSettingsRowsFollowSystemSettingsLayout()
    {
        QWidget parent;
        auto *control = new QPushButton(QStringLiteral("Control"), &parent);
        const QString description = QStringLiteral(
            "A long description whose deterministic wrap width lets the form row grow to fit.");
        QFrame *describedRow = settings::makeRow(
            QStringLiteral("Category"), description, control, &parent);
        auto *descriptionLabel = describedRow->findChild<QLabel *>(
            QStringLiteral("rowDescription"));
        auto *titleLabel = describedRow->findChild<QLabel *>(
            QStringLiteral("rowLabelCell"));
        QVERIFY(descriptionLabel);
        QVERIFY(titleLabel);
        QVERIFY(descriptionLabel->wordWrap());
        QCOMPARE(descriptionLabel->width(),
                 qMin(descriptionLabel->fontMetrics().horizontalAdvance(description),
                      descriptionLabel->fontMetrics().averageCharWidth() * 45));
        QCOMPARE(titleLabel->alignment(), Qt::AlignRight | Qt::AlignVCenter);

        auto *checkBox = new QCheckBox(QStringLiteral("Short label"), &parent);
        const QString sentence = QStringLiteral("Enable the complete setting sentence");
        QFrame *checkBoxRow = settings::makeRow(
            QStringLiteral("Setting"), sentence, checkBox, &parent);
        QCOMPARE(checkBox->text(), sentence);
        QVERIFY(!checkBoxRow->findChild<QLabel *>(QStringLiteral("rowDescription")));
    }
};

int runUiTests(int argc, char **argv)
{
    UiTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_ui.moc"
