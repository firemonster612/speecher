#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "dictation/PopupPresentation.h"
#include "dictation/SelectionEditPresentation.h"
#include "frontend/quick/host/QuickDictation.h"
#include "frontend/quick/host/QuickPopupWindow.h"
#include "frontend/quick/host/QuickSettings.h"
#include "platform/PopupPositioner.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>
#include <QSignalSpy>

#include <algorithm>
#include <memory>

// The UI's QML modules are linked into the tests by test_quick_settings.cpp,
// which imports their plugins for the whole binary.

using namespace speecher;

namespace {

// Says where the popup went and leaves it there.
class RecordingPositioner final : public PopupPositioner {
public:
    using PopupPositioner::PopupPositioner;

    void configurePopup(PopupSurface &) override { ++configured; }
    void positionBottomCenter(PopupSurface &) override { ++positioned; }

    int configured = 0;
    int positioned = 0;
};

QQuickWindow *popupWindow()
{
    for (QWindow *window : QGuiApplication::topLevelWindows()) {
        if (QByteArray(window->metaObject()->className()).startsWith("PopupWindow")) {
            return qobject_cast<QQuickWindow *>(window);
        }
    }
    return nullptr;
}

// The text a run of segments reads as, keeping the kinds given.
QString readAs(const QVariantList &segments, const QStringList &kinds)
{
    QString text;
    for (const QVariant &segment : segments) {
        const QVariantMap map = segment.toMap();
        if (kinds.contains(map.value(QStringLiteral("kind")).toString())) {
            text += map.value(QStringLiteral("text")).toString();
        }
    }
    return text;
}

QString withoutSpaces(QString text)
{
    return text.remove(QRegularExpression(QStringLiteral("\\s")));
}

} // namespace

// The UI's Dictation, over the controller's session, and the popup's window.
// The session is driven through the signals it sends the UI.
class QuickDictationTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Frames without a GPU, so a shown popup really renders one.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    }

    void init()
    {
        SettingsStore settings;
        settings.raw().clear();
    }

    void phasesFollowTheSession_data()
    {
        QTest::addColumn<QString>("sessionState");
        QTest::addColumn<QString>("phase");
        QTest::newRow("starting") << "starting" << "starting";
        QTest::newRow("listening") << "listening" << "listening";
        QTest::newRow("paused") << "paused" << "paused";
        QTest::newRow("stopping") << "stopping" << "transcribing";
        QTest::newRow("refining") << "refining" << "refining";
        QTest::newRow("delivering") << "delivering" << "delivering";
        QTest::newRow("reviewing") << "reviewing" << "review";
        QTest::newRow("error") << "error" << "error";
    }

    void phasesFollowTheSession()
    {
        QFETCH(QString, sessionState);
        QFETCH(QString, phase);
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        DictationSession *session = controller.session();

        emit session->stateChanged(sessionState);
        QCOMPARE(dictation.property("state").toString(), phase);
        emit session->popupHideRequested();
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("idle"));
    }

    void liveWordsLevelAndRefinementPassThrough()
    {
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        DictationSession *session = controller.session();

        emit session->stateChanged(QStringLiteral("listening"));
        emit session->audioLevelChanged(0.5f);
        emit session->previewDisplayChanged(QStringLiteral("brown fox jumps"));
        emit session->popupRefinementPreviewChanged(QStringLiteral("Brown fox"));
        QCOMPARE(dictation.property("level").toReal(), 0.5);
        QCOMPARE(dictation.property("words").toString(), QStringLiteral("brown fox jumps"));
        QCOMPARE(dictation.property("refined").toString(), QStringLiteral("Brown fox"));

        // The next dictation starts with none of this one's refinement, and
        // a hidden popup hears nothing.
        emit session->popupHideRequested();
        QCOMPARE(dictation.property("level").toReal(), 0.0);
        emit session->stateChanged(QStringLiteral("starting"));
        QCOMPARE(dictation.property("refined").toString(), QString());
    }

    void receiptsSayWhatHappened_data()
    {
        QTest::addColumn<QString>("message");
        QTest::addColumn<int>("outcome");
        QTest::addColumn<QString>("fixPage");
        QTest::addColumn<QString>("title");
        QTest::addColumn<QString>("detail");
        QTest::addColumn<QString>("outcomeName");
        QTest::newRow("inserted") << "Input sent" << int(PopupOutcome::Inserted) << "" << "Input sent" << "" << "inserted";
        QTest::newRow("copied, with why") << "Copied • Your selection changed" << int(PopupOutcome::Copied) << ""
                                          << "Copied" << "Your selection changed" << "copied";
        QTest::newRow("fallback, with its fix")
            << "Used raw transcript • Input sent • Your Claude sign-in has expired." << int(PopupOutcome::Fallback)
            << "accounts" << "Used raw transcript" << "Input sent • Your Claude sign-in has expired." << "fallback";
    }

    void receiptsSayWhatHappened()
    {
        QFETCH(QString, message);
        QFETCH(int, outcome);
        QFETCH(QString, fixPage);
        QFETCH(QString, title);
        QFETCH(QString, detail);
        QFETCH(QString, outcomeName);
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        DictationSession *session = controller.session();
        const PopupErrorAction fix = fixPage.isEmpty() ? PopupErrorAction{} : PopupErrorAction{ErrorFix::SettingsPage, fixPage};

        emit session->stateChanged(QStringLiteral("delivering"));
        emit session->popupMessageRequested(message, PopupOutcome(outcome), fix);

        QCOMPARE(dictation.property("state").toString(), QStringLiteral("delivered"));
        const QVariantMap receipt = dictation.property("receipt").toMap();
        QCOMPARE(receipt.value(QStringLiteral("title")).toString(), title);
        QCOMPARE(receipt.value(QStringLiteral("detail")).toString(), detail);
        QCOMPARE(receipt.value(QStringLiteral("outcome")).toString(), outcomeName);
        QCOMPARE(receipt.value(QStringLiteral("page")).toString(), fixPage);
    }

    // The session goes idle before it says "Canceled", and the popup keeps
    // saying it until the session hides it.
    void aCancelsReceiptOutlivesTheIdleSession()
    {
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        DictationSession *session = controller.session();

        emit session->stateChanged(QStringLiteral("listening"));
        emit session->stateChanged(QStringLiteral("idle"));
        emit session->popupMessageRequested(cancelledOutcomeText(), PopupOutcome::Cancelled);
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("delivered"));
        QCOMPARE(dictation.property("receipt").toMap().value(QStringLiteral("outcome")).toString(),
                 QStringLiteral("cancelled"));

        emit session->stateChanged(QStringLiteral("idle"));
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("delivered"));
        emit session->popupHideRequested();
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("idle"));
    }

    void anErrorOffersItsFixAndGoesWhenUsed()
    {
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        QSignalSpy pages(&dictation, &QuickDictation::pageRequested);

        dictation.showError(QStringLiteral("No speech service is available. OpenAI couldn't be reached."),
                            {ErrorFix::SettingsPage, QStringLiteral("speech")});
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("error"));
        const QVariantMap error = dictation.property("error").toMap();
        QCOMPARE(error.value(QStringLiteral("title")).toString(),
                 QStringLiteral("No speech service is available. OpenAI couldn't be reached."));
        QCOMPARE(error.value(QStringLiteral("page")).toString(), QStringLiteral("speech"));

        emit dictation.errorActionRequested();
        QCOMPARE(pages.count(), 1);
        QCOMPARE(pages.first().first().toString(), QStringLiteral("speech"));
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("idle"));
    }

    // Linux has no permission panel to open, so such an error has no button;
    // cancel dismisses it.
    void aPermissionErrorHasNoFixHereAndCancelDismissesIt()
    {
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        QSignalSpy pages(&dictation, &QuickDictation::pageRequested);

        dictation.showError(QStringLiteral("Microphone access is off"), {ErrorFix::MicrophonePermission});
        QCOMPARE(dictation.property("error").toMap().value(QStringLiteral("page")).toString(), QString());
        emit dictation.errorActionRequested();
        QCOMPARE(pages.count(), 0);
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("error"));

        emit dictation.cancelRequested();
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("idle"));
    }

    void aReviewCarriesTheEditAndTheKeysThatAnswerIt()
    {
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        DictationSession *session = controller.session();
        const QString original = QStringLiteral("Can you send me the deck when you get a chance?");
        const QString revised = QStringLiteral("Can you send me the slides when you can?");

        emit session->stateChanged(QStringLiteral("reviewing"));
        emit session->popupSelectionEditReviewRequested(selectionEditReview(
            original, revised, {QStringLiteral("make it shorter")},
            reviewKeysFor(true, true, QString(), QStringLiteral("Ctrl+Space"))));

        QCOMPARE(dictation.property("state").toString(), QStringLiteral("review"));
        const QVariantMap review = dictation.property("review").toMap();
        QCOMPARE(review.value(QStringLiteral("instruction")).toString(), QStringLiteral("“make it shorter”"));
        // Core spaces the marks apart and the popup closes the spaces up
        // again, so the words, not the spacing, must read as each side.
        const QVariantList whole = review.value(QStringLiteral("whole")).toList();
        QCOMPARE(withoutSpaces(readAs(whole, {QStringLiteral("same"), QStringLiteral("added")})), withoutSpaces(revised));
        QCOMPARE(withoutSpaces(readAs(whole, {QStringLiteral("same"), QStringLiteral("removed")})), withoutSpaces(original));
        QVERIFY(readAs(whole, {QStringLiteral("removed")}).contains(QStringLiteral("deck")));
        QVERIFY(readAs(whole, {QStringLiteral("added")}).contains(QStringLiteral("slides")));
        QVERIFY(!review.value(QStringLiteral("summary")).toString().isEmpty());
        QCOMPARE(review.value(QStringLiteral("keepKeys")).toStringList(), QStringList{QStringLiteral("Esc")});
        QCOMPARE(review.value(QStringLiteral("replaceKeys")).toStringList(), QStringList{QStringLiteral("Enter")});
        QCOMPARE(review.value(QStringLiteral("followUpKeys")).toStringList(),
                 (QStringList{QStringLiteral("Ctrl"), QStringLiteral("Space")}));
        QCOMPARE(review.value(QStringLiteral("following")).toBool(), false);
    }

    // Core sends the last few words heard; the popup's words carry on as the
    // first of them drop off, rather than starting over each time.
    void liveWordsCarryOnAsTheFirstWordsDropOff()
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import Speecher.Popup\nLiveWords { width: 4000; maxWords: 40 }", QUrl());
        std::unique_ptr<QObject> live(component.create());
        QVERIFY2(live, qPrintable(component.errorString()));
        const auto shown = [&live] {
            // The words are the Repeater's items, which are visual children only.
            QList<QQuickItem *> words;
            QList<QQuickItem *> items{qobject_cast<QQuickItem *>(live.get())};
            while (!items.isEmpty()) {
                QQuickItem *item = items.takeFirst();
                if (item->property("word").isValid()) {
                    words.append(item);
                }
                items += item->childItems();
            }
            std::sort(words.begin(), words.end(), [](QQuickItem *a, QQuickItem *b) {
                return a->property("index").toInt() < b->property("index").toInt();
            });
            QStringList text;
            for (QQuickItem *word : words) {
                text.append(word->property("word").toString());
            }
            return text.join(u' ');
        };

        live->setProperty("text", QStringLiteral("the quick brown fox"));
        live->setProperty("text", QStringLiteral("quick brown fox jumps"));
        QCOMPARE(shown(), QStringLiteral("the quick brown fox jumps"));
        // The last word heard in part gives way to the whole of it.
        live->setProperty("text", QStringLiteral("brown fox jumps ov"));
        live->setProperty("text", QStringLiteral("fox jumps over the"));
        QCOMPARE(shown(), QStringLiteral("the quick brown fox jumps over the"));
        // Something else altogether starts over.
        live->setProperty("text", QStringLiteral("hello there"));
        QCOMPARE(shown(), QStringLiteral("hello there"));
        // A window whose first word is the text's first word by chance.
        live->setProperty("text", QStringLiteral("the quick the"));
        live->setProperty("text", QStringLiteral("quick the lazy"));
        live->setProperty("text", QStringLiteral("the lazy dog"));
        QCOMPARE(shown(), QStringLiteral("the quick the lazy dog"));
    }

    // The window is configured once, before it first shows, and placed each
    // time; the session hears of the latest dictation's first frame only.
    void onlyTheLatestShownDictationIsPresented()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        QQmlEngine engine;
        auto *positioner = new RecordingPositioner;
        QuickPopupWindow popup(&engine, positioner);
        QSignalSpy presented(&popup, &QuickPopupWindow::presented);
        QCOMPARE(positioner->configured, 1);

        emit controller.session()->stateChanged(QStringLiteral("starting"));
        popup.show(1);
        popup.show(2);
        QTRY_COMPARE(presented.count(), 1);
        QCOMPARE(presented.first().first().toULongLong(), 2ull);
        QCOMPARE(positioner->positioned, 2);
        QTest::qWait(200);
        QCOMPARE(presented.count(), 1);
    }

    void aPopupHiddenBeforeItsFrameIsNotPresented()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        QQmlEngine engine;
        QuickPopupWindow popup(&engine, new RecordingPositioner);
        QSignalSpy presented(&popup, &QuickPopupWindow::presented);

        emit controller.session()->stateChanged(QStringLiteral("starting"));
        popup.show(3);
        popup.hide();
        QTest::qWait(200);
        QCOMPARE(presented.count(), 0);

        popup.show(4);
        QTRY_COMPARE(presented.count(), 1);
        QCOMPARE(presented.first().first().toULongLong(), 4ull);
    }

    // The window is as big as the popup ever grows; only the capsule, at
    // the bottom centre inside the shadow's 32 px, takes the pointer.
    void onlyTheCapsuleTakesThePointer()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        QQmlEngine engine;
        QuickPopupWindow popup(&engine, new RecordingPositioner);
        DictationSession *session = controller.session();
        emit session->stateChanged(QStringLiteral("listening"));
        popup.show(1);
        QQuickWindow *window = popupWindow();
        QVERIFY(window);

        const auto capsuleIs = [window](int width, int height) {
            const QRegion mask = window->mask();
            const QRect capsule(QPoint((window->width() - width) / 2, window->height() - 32 - height), QSize(width, height));
            return mask == QRegion(capsule);
        };
        // Listening with no words yet: the bare bar.
        QTRY_VERIFY(capsuleIs(360, 58));
        // The receipt's capsule hugs a short line.
        emit session->popupMessageRequested(QStringLiteral("Input sent"), PopupOutcome::Inserted);
        QTRY_VERIFY(capsuleIs(320, 64));

        // Faded out, the window goes.
        emit session->popupHideRequested();
        QTRY_VERIFY(!window->isVisible());
    }
};

int runQuickDictationTests(int argc, char **argv)
{
    QuickDictationTests suite;
    return runTestSuite(&suite, argc, argv);
}

#include "test_quick_dictation.moc"
