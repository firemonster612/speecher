#include "common/test_doubles.h"
#include "common/test_http.h"
#include "common/test_auth.h"
#include "ui/AccessibilityNotice.h"
#include "ui/settings/ApplicationSettingsPage.h"
#include "ui/settings/CorrectionsSettingsPage.h"
#include "ui/settings/OutputSettingsPage.h"
#include "ui/settings/RefinementSettingsPage.h"

#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
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

    void settingsDialogUsesKdePageWidgetOnPlasma()
    {
#ifdef SPEECHER_WITH_KPAGEWIDGET
        const QByteArray previousDesktop = qgetenv("XDG_CURRENT_DESKTOP");
        qputenv("XDG_CURRENT_DESKTOP", "KDE");
        const auto restoreDesktop = qScopeGuard([previousDesktop] {
            if (previousDesktop.isNull()) {
                qunsetenv("XDG_CURRENT_DESKTOP");
            } else {
                qputenv("XDG_CURRENT_DESKTOP", previousDesktop);
            }
        });

        ApplicationController controller(true);
        SettingsDialog dialog(&controller);
        auto *pages = dialog.findChild<KPageWidget *>(QStringLiteral("settingsPages"));
        QVERIFY(pages);
        QCOMPARE(pages->faceType(), KPageView::FlatList);
        QCOMPARE(pages->model()->rowCount(), 7);
        auto *resizeHandle = pages->findChild<QWidget *>(
            QStringLiteral("settingsSidebarResizeHandle"));
        QVERIFY(resizeHandle);
        QCOMPARE(resizeHandle->cursor().shape(), Qt::SplitHCursor);
        dialog.resize(1200, 780);
        dialog.show();
        QCoreApplication::processEvents();
        auto *searchContainer = pages->findChild<QWidget *>(
            QStringLiteral("KPageView::Search"));
        QVERIFY(searchContainer);
        auto *headerSeparator = pages->findChild<QWidget *>(
            QStringLiteral("settingsHeaderSeparator"));
        QVERIFY(headerSeparator);
        QCOMPARE(headerSeparator->height(), 1);
        QCOMPARE(headerSeparator->width(), pages->width());
        auto *navigationView = pages->findChild<QAbstractItemView *>(
            QString(),
            Qt::FindDirectChildrenOnly);
        QVERIFY(navigationView);
        QImage separatorImage(resizeHandle->size(), QImage::Format_ARGB32_Premultiplied);
        separatorImage.fill(Qt::transparent);
        resizeHandle->render(
            &separatorImage,
            QPoint(),
            QRegion(),
            QWidget::DrawChildren);
        const int separatorX = resizeHandle->width() / 2;
        QVector<QPair<int, int>> paintedRuns;
        int runStart = -1;
        for (int y = 0; y < separatorImage.height(); ++y) {
            const bool painted = separatorImage.pixelColor(separatorX, y).alpha() > 0;
            if (painted && runStart < 0) {
                runStart = y;
            } else if (!painted && runStart >= 0) {
                paintedRuns.append({runStart, y - 1});
                runStart = -1;
            }
        }
        if (runStart >= 0) {
            paintedRuns.append({runStart, separatorImage.height() - 1});
        }
        QCOMPARE(paintedRuns.size(), 2);
        QVERIFY(paintedRuns.first().first > 0);
        QVERIFY(paintedRuns.first().second
                < searchContainer->geometry().bottom());
        QVERIFY(paintedRuns.last().first
                <= searchContainer->geometry().bottom() + 1);
        QCOMPARE(paintedRuns.last().second, separatorImage.height() - 1);
        const int initialSidebarWidth = searchContainer->width();
        const int initialNavigationWidth = navigationView->width();
        const QPointF localPosition = resizeHandle->rect().center();
        const QPointF globalPosition = resizeHandle->mapToGlobal(
            localPosition.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress,
                          localPosition,
                          globalPosition,
                          Qt::LeftButton,
                          Qt::LeftButton,
                          Qt::NoModifier);
        QCoreApplication::sendEvent(resizeHandle, &press);
        QMouseEvent move(QEvent::MouseMove,
                         localPosition,
                         globalPosition + QPointF(60, 0),
                         Qt::NoButton,
                         Qt::LeftButton,
                         Qt::NoModifier);
        QCoreApplication::sendEvent(resizeHandle, &move);
        QVERIFY(searchContainer->width() > initialSidebarWidth);
        QVERIFY(navigationView->width() > initialNavigationWidth);
        QCOMPARE(navigationView->width(), searchContainer->width());
        QVERIFY(dialog.findChildren<QComboBox *>(
                          QString(),
                          Qt::FindDirectChildrenOnly)
                    .isEmpty());
#else
        QSKIP("KPageWidget is not available in this build");
#endif
    }

    void settingsDialogUsesPlatformStyledSidebarOutsidePlasma()
    {
        const QByteArray previousDesktop = qgetenv("XDG_CURRENT_DESKTOP");
        qputenv("XDG_CURRENT_DESKTOP", "GNOME");
        const auto restoreDesktop = qScopeGuard([previousDesktop] {
            if (previousDesktop.isNull()) {
                qunsetenv("XDG_CURRENT_DESKTOP");
            } else {
                qputenv("XDG_CURRENT_DESKTOP", previousDesktop);
            }
        });

        ApplicationController controller(true);
        SettingsDialog dialog(&controller);
        auto *categories = dialog.findChild<QListWidget *>(
            QStringLiteral("settingsCategories"));
        QVERIFY(categories);
        QCOMPARE(categories->count(), 7);
        QVERIFY(categories->styleSheet().isEmpty());
        QVERIFY(!dialog.findChild<QWidget *>(
            QStringLiteral("settingsSidebarResizeHandle")));
#ifdef SPEECHER_WITH_KPAGEWIDGET
        QVERIFY(!dialog.findChild<KPageWidget *>(QStringLiteral("settingsPages")));
#endif
    }

    void settingsDialogLeavesControlsToThePlatformStyle()
    {
        ApplicationController controller(true);
        SettingsDialog dialog(&controller);
        for (QWidget *widget : dialog.findChildren<QWidget *>()) {
            QVERIFY2(widget->styleSheet().isEmpty(),
                     qPrintable(QStringLiteral("%1 has an application stylesheet")
                                    .arg(widget->objectName().isEmpty()
                                             ? QString::fromLatin1(widget->metaObject()->className())
                                             : widget->objectName())));
        }
    }
};

int runUiTests(int argc, char **argv)
{
    UiTests tests;
    return runTestSuite(&tests, argc, argv);
}

#include "test_ui.moc"
