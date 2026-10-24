#pragma once

#include "dictation/PopupPresentation.h"

#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantMap>

namespace speecher {

class ApplicationController;
struct SelectionEditReview;

// The UI's DictationState: a dictation as the popup shows it, in plain
// values and with the popup's commands as signals. Dictation, the live one,
// fills it from the session; a page makes its own to play a made-up
// dictation in a preview, and nothing connects that one to the session. The
// UI words every phase itself.
class QuickDictationState : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(DictationState)
    // idle (no popup), starting, listening, paused, transcribing, refining,
    // delivering, delivered (a receipt on show, a cancel's too), review or
    // error.
    Q_PROPERTY(QString state MEMBER m_state NOTIFY stateChanged)
    // The input level, 0 to 1.
    Q_PROPERTY(qreal level MEMBER m_level NOTIFY levelChanged)
    // Seconds listened, pauses left out.
    Q_PROPERTY(int elapsed MEMBER m_elapsed NOTIFY elapsedChanged)
    // The last words heard, as many as the Live text length setting keeps;
    // empty while live text is off.
    Q_PROPERTY(QString words MEMBER m_words NOTIFY wordsChanged)
    // The last words of the refined text as it streams in.
    Q_PROPERTY(QString refined MEMBER m_refined NOTIFY refinedChanged)
    // delivered: { title, detail, outcome, page }. outcome is inserted,
    // copied (on the clipboard only), fallback (another provider or the raw
    // transcript stood in) or cancelled; page is the settings page that
    // fixes what a fallback stood in for, or empty.
    Q_PROPERTY(QVariantMap receipt MEMBER m_receipt NOTIFY receiptChanged)
    // review: the edit to a selection. { instruction (what was said,
    // quoted), whole and folded (the edit as segments, { kind: same|added|
    // removed, text }; folded keeps the changes and a few words around them,
    // and is empty when the edit is short), summary ("2 changes"), keepKeys,
    // replaceKeys and followUpKeys (the keys that answer it, each a list,
    // empty for none), following (a follow-up is being dictated) }.
    Q_PROPERTY(QVariantMap review MEMBER m_review NOTIFY reviewChanged)
    // error: { title, detail, page }, page the settings page that fixes it,
    // or empty.
    Q_PROPERTY(QVariantMap error MEMBER m_error NOTIFY errorChanged)

public:
    explicit QuickDictationState(QObject *parent = nullptr);

signals:
    void stateChanged();
    void levelChanged();
    void elapsedChanged();
    void wordsChanged();
    void refinedChanged();
    void receiptChanged();
    void reviewChanged();
    void errorChanged();

    void startRequested();
    void stopRequested();
    // Pauses, or resumes while paused.
    void pauseRequested();
    // Throws the dictation away, keeps the original under review, or
    // dismisses an error.
    void cancelRequested();
    // review: puts the edit in place of the selection.
    void replaceRequested();
    // review: dictates a further instruction for the same selection.
    void followUpRequested();
    // delivered or error: opens the page that fixes it.
    void errorActionRequested();

protected:
    QString m_state = QStringLiteral("idle");
    qreal m_level = 0;
    int m_elapsed = 0;
    QString m_words;
    QString m_refined;
    QVariantMap m_receipt;
    QVariantMap m_review;
    QVariantMap m_error;
};

// The UI's Dictation singleton: the controller's Dictation Session as the
// popup shows it, and the popup's commands carried to the controller and the
// session. The popup comes and goes as the session asks; an error waits
// popupErrorDismissMs before it dismisses itself.
class QuickDictation final : public QuickDictationState {
    Q_OBJECT
    QML_NAMED_ELEMENT(Dictation)
    QML_SINGLETON

public:
    explicit QuickDictation(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickDictation() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickDictation *create(QQmlEngine *, QJSEngine *);

    // An error from outside the session, such as a start the microphone was
    // refused for. The host shows the popup for it.
    void showError(const QString &message, const PopupErrorAction &fix = {});

signals:
    // The settings page an error or a receipt offers as its fix.
    void pageRequested(const QString &pageId);

private:
    void followSession(const QString &stateName);
    void showReceipt(const QString &message, PopupOutcome outcome, const PopupErrorAction &fix);
    void setError(const QString &message, const PopupErrorAction &fix);
    void setReview(const SelectionEditReview &review);
    void dismissError();
    void openFix();
    void cancel();
    void updateElapsed();
    void setState(const QString &state);
    template <typename T>
    void update(T &member, const T &value, void (QuickDictationState::*changed)());

    ApplicationController *m_controller;
    QTimer m_errorDismissal;
    // Ticks elapsed while listening.
    QTimer m_clock;
};

} // namespace speecher
