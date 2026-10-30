#include "common/test_suites.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "dictation/PopupPresentation.h"
#include "dictation/SelectionEditPresentation.h"
#include "frontend/quick/host/QuickDictation.h"
#include "frontend/quick/host/QuickPopupWindow.h"
#include "frontend/quick/host/QuickSettings.h"
#include "frontend/quick/host/QuickUpdates.h"
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
#include <optional>
#include <utility>

// The UI's QML modules are linked into the tests by QuickFrontEnd.cpp, which
// imports their plugins for the whole binary.

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

QQuickItem *capsuleOf(QQuickWindow *window)
{
    return window->findChild<QQuickItem *>(QStringLiteral("capsule"));
}

// The size the capsule settles at for the popup's phase; a receipt's hugs
// its words, which each platform's fonts set.
QSizeF settledSize(QQuickItem *capsule)
{
    return capsule->parentItem()->property("target").toSizeF();
}

// The key a review's Replace names: the Mac keyboard's Return is Enter
// elsewhere.
#ifdef Q_OS_MACOS
const QString replaceKey = QStringLiteral("Return");
#else
const QString replaceKey = QStringLiteral("Enter");
#endif

// Where the popup window takes the pointer. On Windows the mask takes in the
// capsule's shadow too, 20 px at the sides, 12 above and 28 below; macOS has
// no mask, and MacClickThrough is given the window's inputRegion.
QRect pointerRect(QQuickWindow *window)
{
#if defined(Q_OS_MACOS)
    return window->property("inputRegion").toRectF().toAlignedRect();
#elif defined(Q_OS_WIN)
    return window->mask().boundingRect().adjusted(20, 12, -20, -28);
#else
    return window->mask().boundingRect();
#endif
}

QPoint centreOf(QQuickItem *item)
{
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
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

    void cleanup()
    {
        QuickDictation::setErrorDismissMs(0);
    }

    void phasesFollowTheSession_data()
    {
        QTest::addColumn<DictationState>("sessionState");
        QTest::addColumn<QString>("phase");
        QTest::newRow("starting") << DictationState::Starting << "starting";
        QTest::newRow("listening") << DictationState::Listening << "listening";
        QTest::newRow("paused") << DictationState::Paused << "paused";
        QTest::newRow("stopping") << DictationState::Stopping << "transcribing";
        QTest::newRow("refining") << DictationState::Refining << "refining";
        QTest::newRow("delivering") << DictationState::Delivering << "delivering";
        QTest::newRow("reviewing") << DictationState::Reviewing << "review";
        QTest::newRow("error") << DictationState::Error << "error";
    }

    void phasesFollowTheSession()
    {
        QFETCH(DictationState, sessionState);
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

        emit session->stateChanged(DictationState::Listening);
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
        emit session->stateChanged(DictationState::Starting);
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
        QTest::newRow("inserted") << "Sent to the app" << int(PopupOutcome::Inserted) << "" << "Sent to the app" << "" << "inserted";
        QTest::newRow("copied, with why") << "Copied • Your selection changed" << int(PopupOutcome::Copied) << ""
                                          << "Copied" << "Your selection changed" << "copied";
        QTest::newRow("fallback, with its fix")
            << "Used raw transcript • Sent to the app • Your Claude sign-in has expired." << int(PopupOutcome::Fallback)
            << "accounts" << "Used raw transcript" << "Sent to the app • Your Claude sign-in has expired." << "fallback";
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

        emit session->stateChanged(DictationState::Delivering);
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

        emit session->stateChanged(DictationState::Listening);
        emit session->stateChanged(DictationState::Idle);
        emit session->popupMessageRequested(cancelledOutcomeText(), PopupOutcome::Cancelled);
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("delivered"));
        QCOMPARE(dictation.property("receipt").toMap().value(QStringLiteral("outcome")).toString(),
                 QStringLiteral("cancelled"));

        emit session->stateChanged(DictationState::Idle);
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

    // A refused microphone's fix is Permissions & health, on every platform.
    void aMicrophonePermissionErrorOpensPermissions()
    {
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        QSignalSpy pages(&dictation, &QuickDictation::pageRequested);

        dictation.showError(QStringLiteral("Microphone access is off"), {ErrorFix::MicrophonePermission});
        QCOMPARE(dictation.property("error").toMap().value(QStringLiteral("page")).toString(),
                 QStringLiteral("permissions"));
        emit dictation.errorActionRequested();
        QCOMPARE(pages.count(), 1);
        QCOMPARE(pages.first().first().toString(), QStringLiteral("permissions"));
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("idle"));
    }

    // An error without a fix has no button; cancel dismisses it.
    void anErrorWithoutAFixHasNoButtonAndCancelDismissesIt()
    {
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        QSignalSpy pages(&dictation, &QuickDictation::pageRequested);

        dictation.showError(QStringLiteral("Couldn't start"));
        QCOMPARE(dictation.property("error").toMap().value(QStringLiteral("page")).toString(), QString());
        emit dictation.errorActionRequested();
        QCOMPARE(pages.count(), 0);
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("error"));

        emit dictation.cancelRequested();
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("idle"));
    }

    // An error from outside the session goes by itself once its time is up.
    void anErrorFromOutsideTheSessionDismissesItself()
    {
        QuickDictation::setErrorDismissMs(50);
        ApplicationController controller(true);
        QuickDictation dictation(&controller);

        dictation.showError(QStringLiteral("Microphone access is off"), {ErrorFix::MicrophonePermission});
        QCOMPARE(dictation.property("error").toMap().value(QStringLiteral("dismissMs")).toInt(), 50);
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("error"));
        QTRY_COMPARE(dictation.property("state").toString(), QStringLiteral("idle"));
    }

    // The session's own error ends through the session, which hides the
    // popup, as its cancel would.
    void aSessionsErrorDismissesItselfThroughTheSession()
    {
        QuickDictation::setErrorDismissMs(50);
        // Not built in here, so the dictation cannot start.
        SettingsStore().setSpeechProvider(QStringLiteral("local"));
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        DictationSession *session = controller.session();
        QSignalSpy hidden(session, &DictationSession::popupHideRequested);
        // Taken as the error shows, which is up for only 50 ms: polling can
        // miss it on a busy machine.
        std::optional<DictationState> sessionWhenShown;
        connect(&dictation, &QuickDictation::stateChanged, &dictation, [&] {
            if (dictation.property("state").toString() == QLatin1String("error")) {
                sessionWhenShown = session->state();
            }
        });

        // Straight to the session: the controller on macOS first asks for
        // the microphone, which a bare test binary can't be given.
        session->startListening();
        QTRY_COMPARE(hidden.count(), 1);
        QVERIFY(sessionWhenShown == DictationState::Error);
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("idle"));
    }

    // A session error with no words still shows as an error, with nothing
    // of the last one, and still goes.
    void anErrorWithoutWordsShowsNothingOfTheLastAndGoes()
    {
        QuickDictation::setErrorDismissMs(50);
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        DictationSession *session = controller.session();
        dictation.showError(QStringLiteral("Your Claude sign-in has expired."), {ErrorFix::SettingsPage, QStringLiteral("accounts")});
        emit dictation.cancelRequested();

        emit session->stateChanged(DictationState::Starting);
        emit session->stateChanged(DictationState::Error);
        QCOMPARE(dictation.property("state").toString(), QStringLiteral("error"));
        const QVariantMap error = dictation.property("error").toMap();
        QCOMPARE(error.value(QStringLiteral("title")).toString(), QString());
        QCOMPARE(error.value(QStringLiteral("page")).toString(), QString());
        QTRY_COMPARE(dictation.property("state").toString(), QStringLiteral("idle"));
    }

    // An error offers Dismiss beside its countdown, which waits while the
    // pointer rests on the capsule, also when the error shows under it.
    // A long notice scrolls; the next one starts at its first line.
    void eachNoticeStartsAtItsTop()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        // The popup's offers read Updates, as the host always has it.
        QuickUpdates updates(&controller, controller.updates());
        QQmlEngine engine;
        QuickPopupWindow popup(&engine, new RecordingPositioner);
        dictation.showError(QStringLiteral("OpenAI couldn't be reached. ").repeated(40));
        popup.show(0);
        QQuickWindow *window = popupWindow();
        QVERIFY(window);
        auto *scroll = window->findChild<QQuickItem *>(QStringLiteral("noticeScroll"));
        QVERIFY(scroll);
        QTRY_VERIFY(scroll->property("contentHeight").toReal() > scroll->height());
        scroll->setProperty("contentY", scroll->property("contentHeight").toReal() - scroll->height());

        dictation.showError(QStringLiteral("Claude couldn't be reached. ").repeated(40));
        QTRY_COMPARE(scroll->property("contentY").toReal(), 0.0);
    }

    // The same failure again is a fresh one: its countdown starts over.
    void theSameErrorAgainCountsDownFromTheStart()
    {
        // The first countdown outlasts the test; only a fresh, short one for
        // the repeat dismisses the error.
        QuickDictation::setErrorDismissMs(60000);
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        const auto state = [&dictation] { return dictation.property("state").toString(); };
        QSignalSpy changed(&dictation, &QuickDictation::errorChanged);
        dictation.showError(QStringLiteral("Dictation is unavailable while files are being transcribed."));
        // Exactly the same error again is shown again, not passed over.
        dictation.showError(QStringLiteral("Dictation is unavailable while files are being transcribed."));
        QCOMPARE(changed.count(), 2);
        QuickDictation::setErrorDismissMs(300);
        dictation.showError(QStringLiteral("Dictation is unavailable while files are being transcribed."));
        QTRY_COMPARE(state(), QStringLiteral("idle"));
    }

    void anErrorsCountdownWaitsUnderThePointerAndDismissCancels()
    {
        // No runner outlasts this countdown while the popup first shows.
        QuickDictation::setErrorDismissMs(60000);
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        // The popup's offers read Updates, as the host always has it.
        QuickUpdates updates(&controller, controller.updates());
        QQmlEngine engine;
        QuickPopupWindow popup(&engine, new RecordingPositioner);
        const auto state = [&dictation] { return dictation.property("state").toString(); };
        dictation.showError(QStringLiteral("OpenAI couldn't be reached."));
        popup.show(0);
        QQuickWindow *window = popupWindow();
        QVERIFY(window);
        QQuickItem *capsule = capsuleOf(window);
        QVERIFY(capsule);
        QTRY_COMPARE(capsule->opacity(), 1.0);
        // Shown again once the capsule is up: the countdown restarts, short.
        QuickDictation::setErrorDismissMs(300);
        dictation.showError(QStringLiteral("OpenAI couldn't be reached."));

        QTest::mouseMove(window, centreOf(capsule));
        QTest::qWait(600);
        QCOMPARE(state(), QStringLiteral("error"));
        QTest::mouseMove(window, QPoint(1, 1));
        QTRY_COMPARE(state(), QStringLiteral("idle"));

        // The pointer is already there when the next one shows, and holds
        // it. Long, so the hold, however late the popup's first frame, is
        // what this waits for rather than a race with the countdown.
        QuickDictation::setErrorDismissMs(60000);
        QSignalSpy holds(&dictation, &QuickDictation::errorHoldRequested);
        QTest::mouseMove(window, centreOf(capsule));
        dictation.showError(QStringLiteral("OpenAI couldn't be reached."));
        popup.show(0);
        QTRY_VERIFY(!holds.isEmpty() && holds.last().first().toBool());
        QCOMPARE(state(), QStringLiteral("error"));
        // Held already, a short countdown for the same error waits too.
        QuickDictation::setErrorDismissMs(300);
        dictation.showError(QStringLiteral("OpenAI couldn't be reached."));
        QTest::qWait(600);
        QCOMPARE(state(), QStringLiteral("error"));

        QQuickItem *dismiss = nullptr;
        for (QList<QQuickItem *> items{capsule}; !items.isEmpty() && !dismiss;) {
            QQuickItem *item = items.takeFirst();
            if (item->isVisible() && item->property("text").toString() == QStringLiteral("Dismiss")) {
                dismiss = item;
            }
            items += item->childItems();
        }
        QVERIFY(dismiss);
        QMetaObject::invokeMethod(dismiss, "clicked");
        QCOMPARE(state(), QStringLiteral("idle"));
    }

    void aReviewCarriesTheEditAndTheKeysThatAnswerIt()
    {
        ApplicationController controller(true);
        QuickDictation dictation(&controller);
        DictationSession *session = controller.session();
        const QString original = QStringLiteral("Can you send me the deck when you get a chance?");
        const QString revised = QStringLiteral("Can you send me the slides when you can?");

        emit session->stateChanged(DictationState::Reviewing);
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
        QCOMPARE(review.value(QStringLiteral("replaceKeys")).toStringList(), QStringList{replaceKey});
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
        // The popup's offers read Updates, as the host always has it.
        QuickUpdates updates(&controller, controller.updates());
        QQmlEngine engine;
        auto *positioner = new RecordingPositioner;
        QuickPopupWindow popup(&engine, positioner);
        QSignalSpy presented(&popup, &QuickPopupWindow::presented);
        QCOMPARE(positioner->configured, 1);

        emit controller.session()->stateChanged(DictationState::Starting);
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
        // The popup's offers read Updates, as the host always has it.
        QuickUpdates updates(&controller, controller.updates());
        QQmlEngine engine;
        QuickPopupWindow popup(&engine, new RecordingPositioner);
        QSignalSpy presented(&popup, &QuickPopupWindow::presented);

        emit controller.session()->stateChanged(DictationState::Starting);
        popup.show(3);
        popup.hide();
        QTest::qWait(200);
        QCOMPARE(presented.count(), 0);

        popup.show(4);
        QTRY_COMPARE(presented.count(), 1);
        QCOMPARE(presented.first().first().toULongLong(), 4ull);
    }

    // A frame of an earlier show whose answer is still on its way when the
    // popup is hidden and shown again does not answer for the later show.
    // The test plays the render loop's signals itself: offscreen, a frame of
    // the later show would render before the answer arrived.
    void aQueuedFrameOfAnEarlierShowDoesNotPresentALaterOne()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        // The popup's offers read Updates, as the host always has it.
        QuickUpdates updates(&controller, controller.updates());
        QQmlEngine engine;
        QuickPopupWindow popup(&engine, new RecordingPositioner);
        QQuickWindow *window = popupWindow();
        QVERIFY(window);
        QSignalSpy presented(&popup, &QuickPopupWindow::presented);
        emit controller.session()->stateChanged(DictationState::Starting);

        popup.show(1);
        emit window->afterSynchronizing();
        emit window->frameSwapped();
        popup.hide();
        popup.show(2);
        // The first show's answer, and nothing else yet.
        QCoreApplication::sendPostedEvents(&popup);
        QCOMPARE(presented.count(), 0);

        QTRY_COMPARE(presented.count(), 1);
        QCOMPARE(presented.first().first().toULongLong(), 2ull);
    }

    // A frame synchronized before a show and swapped after it shows what was
    // there before; the show waits for the next one.
    void aFrameInFlightAtAShowDoesNotPresentIt()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        // The popup's offers read Updates, as the host always has it.
        QuickUpdates updates(&controller, controller.updates());
        QQmlEngine engine;
        QuickPopupWindow popup(&engine, new RecordingPositioner);
        QQuickWindow *window = popupWindow();
        QVERIFY(window);
        QSignalSpy presented(&popup, &QuickPopupWindow::presented);
        emit controller.session()->stateChanged(DictationState::Starting);
        popup.show(1);
        QTRY_COMPARE(presented.count(), 1);

        bool showDuringNextSync = false;
        int syncsSinceReshow = -1;
        int syncsWhenPresented = -1;
        connect(window, &QQuickWindow::afterSynchronizing, &popup, [&] {
            if (syncsSinceReshow >= 0) {
                ++syncsSinceReshow;
            } else if (std::exchange(showDuringNextSync, false)) {
                popup.show(2);
                syncsSinceReshow = 0;
            }
        }, Qt::DirectConnection);
        connect(&popup, &QuickPopupWindow::presented, &popup, [&] { syncsWhenPresented = syncsSinceReshow; });
        showDuringNextSync = true;
        window->update();
        QTRY_COMPARE(presented.count(), 2);
        QCOMPARE(presented.last().first().toULongLong(), 2ull);
        QVERIFY2(syncsWhenPresented >= 1, "presented by the frame synchronized before the show");
    }

    // Before any review, Dictation names the keys the configured shortcuts
    // give one, so the window is as wide as its buttons when it shows and a
    // review keeps the capsule where it is; one answered by longer keys
    // places the window again.
    void aReviewKeepsThePopupWhereItShowed()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        const QVariantMap idle = dictation.property("review").toMap();
        QCOMPARE(idle.value(QStringLiteral("keepKeys")).toStringList(), QStringList{QStringLiteral("Esc")});
        QCOMPARE(idle.value(QStringLiteral("replaceKeys")).toStringList(), QStringList{replaceKey});
        QCOMPARE(idle.value(QStringLiteral("followUpKeys")).toStringList(), shortcutKeys(controller.globalShortcutDisplay()));

        // The popup's offers read Updates, as the host always has it.
        QuickUpdates updates(&controller, controller.updates());
        QQmlEngine engine;
        auto *positioner = new RecordingPositioner;
        QuickPopupWindow popup(&engine, positioner);
        QQuickWindow *window = popupWindow();
        QVERIFY(window);
        DictationSession *session = controller.session();
        const auto review = [session, &controller](bool escapeHeld, const QString &cancelShortcut) {
            emit session->stateChanged(DictationState::Reviewing);
            emit session->popupSelectionEditReviewRequested(selectionEditReview(
                QStringLiteral("the deck"), QStringLiteral("the slides"), {QStringLiteral("say slides")},
                reviewKeysFor(escapeHeld, true, cancelShortcut, controller.globalShortcutDisplay())));
        };

        emit session->stateChanged(DictationState::Starting);
        const QSize shown = window->size();
        popup.show(1);
        QTRY_VERIFY(window->isExposed());
        review(true, QString());
        QTest::qWait(200);
        QCOMPARE(window->size(), shown);
        QCOMPARE(positioner->positioned, 1);

        // Escape not held: a long Cancel Shortcut keeps the original.
        review(false, QStringLiteral("Ctrl+Alt+Shift+Meta+Backspace"));
        QTRY_VERIFY(window->width() > shown.width());
        QCOMPARE(positioner->positioned, 2);
    }

    // The capsule takes the pointer wherever it is drawn, also while it
    // arrives scaled and lifted and while it grows or shrinks to the next
    // phase.
    void theInputRegionFollowsTheCapsuleAsItMorphs()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        // The popup's offers read Updates, as the host always has it.
        QuickUpdates updates(&controller, controller.updates());
        QQmlEngine engine;
        QuickPopupWindow popup(&engine, new RecordingPositioner);
        DictationSession *session = controller.session();
        emit session->stateChanged(DictationState::Listening);
        popup.show(1);
        QQuickWindow *window = popupWindow();
        QVERIFY(window);
        QQuickItem *capsule = capsuleOf(window);
        QVERIFY(capsule);
        // The pointer is taken where it is drawn, its scale and lift
        // included, to within the pixel the region rounds out to.
        const auto masksDrawn = [window, capsule] {
            const QRectF drawn = capsule->mapRectToScene(QRectF(0, 0, capsule->width(), capsule->height()));
            const QRect mask = pointerRect(window);
            return qAbs(mask.left() - drawn.left()) <= 1 && qAbs(mask.top() - drawn.top()) <= 1
                && qAbs(mask.right() + 1 - drawn.right()) <= 1 && qAbs(mask.bottom() + 1 - drawn.bottom()) <= 1;
        };
        // Arriving: every frame while it is still smaller than it settles.
        int arrivingFrames = 0;
        bool everyFrameMasked = true;
        connect(capsule, &QQuickItem::scaleChanged, &popup, [&] {
            if (capsule->scale() < 0.99) {
                ++arrivingFrames;
                everyFrameMasked = everyFrameMasked && masksDrawn();
            }
        });
        QTRY_COMPARE(capsule->width(), 280.0);
        QTRY_COMPARE(capsule->scale(), 1.0);
        QVERIFY(arrivingFrames > 0);
        QVERIFY(everyFrameMasked);

        // From the bare bar down to the receipt, which hugs its words: every
        // frame on the way.
        int morphingFrames = 0;
        bool everyMorphingFrameMasked = true;
        connect(capsule, &QQuickItem::widthChanged, &popup, [&] {
            ++morphingFrames;
            everyMorphingFrameMasked = everyMorphingFrameMasked && masksDrawn();
        });
        emit session->popupMessageRequested(QStringLiteral("Sent to the app"), PopupOutcome::Inserted);
        QTRY_VERIFY(settledSize(capsule).width() < 280);
        QTRY_COMPARE(capsule->width(), settledSize(capsule).width());
        QVERIFY(morphingFrames > 1);
        QVERIFY(everyMorphingFrameMasked);
    }

    // The window is as big as the popup ever grows; its mask follows the
    // capsule, at the bottom centre inside the shadow's 32 px, so clicks
    // beside it go through.
    void theMaskFollowsTheCapsule()
    {
        ApplicationController controller(true);
        QuickSettings settings(&controller);
        QuickDictation dictation(&controller);
        // The popup's offers read Updates, as the host always has it.
        QuickUpdates updates(&controller, controller.updates());
        QQmlEngine engine;
        QuickPopupWindow popup(&engine, new RecordingPositioner);
        DictationSession *session = controller.session();
        emit session->stateChanged(DictationState::Listening);
        popup.show(1);
        QQuickWindow *window = popupWindow();
        QVERIFY(window);

        const auto capsuleIs = [window](int width, int height) {
            const QRegion mask = window->mask();
            const QRect capsule(QPoint((window->width() - width) / 2, window->height() - 32 - height), QSize(width, height));
#if defined(Q_OS_MACOS)
            // Cocoa's mask would clip the shadow; the window follows the
            // pointer instead.
            Q_UNUSED(capsule);
            return mask.isEmpty();
#elif defined(Q_OS_WIN)
            // Windows' mask clips drawing too, so it takes in the shadow: 20
            // px at the sides, 12 above and 28 below.
            return mask == QRegion(capsule.adjusted(-20, -12, 20, 28));
#else
            return mask == QRegion(capsule);
#endif
        };
        // Listening with no words yet: the bare bar.
        QTRY_VERIFY(capsuleIs(280, 44));
        // The receipt's capsule hugs a short line, narrower than the bar.
        emit session->popupMessageRequested(QStringLiteral("Sent to the app"), PopupOutcome::Inserted);
        auto *capsule = window->findChild<QQuickItem *>(QStringLiteral("capsule"));
        QVERIFY(capsule);
        QTRY_VERIFY(settledSize(capsule).width() < 280);
        QTRY_COMPARE(capsule->width(), settledSize(capsule).width());
        // Its width is fractional, so the mask rounds out to within a pixel.
        const auto masksCapsule = [window, capsule] {
#if defined(Q_OS_MACOS)
            Q_UNUSED(capsule);
            return window->mask().isEmpty();
#else
            QRectF drawn = capsule->mapRectToScene(QRectF(0, 0, capsule->width(), capsule->height()));
#if defined(Q_OS_WIN)
            drawn.adjust(-20, -12, 20, 28);
#endif
            const QRect mask = window->mask().boundingRect();
            return qAbs(mask.left() - drawn.left()) <= 1 && qAbs(mask.top() - drawn.top()) <= 1
                && qAbs(mask.right() + 1 - drawn.right()) <= 1 && qAbs(mask.bottom() + 1 - drawn.bottom()) <= 1;
#endif
        };
        QTRY_VERIFY(masksCapsule());

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
