#include "frontend/quick/host/QuickDictation.h"

#include "app/ApplicationController.h"
#include "dictation/DictationSession.h"
#include "dictation/SelectionEditPresentation.h"
#include "frontend/quick/host/QmlValues.h"
#include "frontend/quick/host/QuickSettings.h"

#include <QDebug>

#include <utility>

namespace speecher {

namespace {

constexpr int msPerSecond = 1000;
int g_errorDismissMs = 0;

// Each phase by the name QML reads.
const struct PhaseName {
    const char *id;
    PopupPhase phase;
} phaseNames[] = {
    {"idle", PopupPhase::Idle},
    {"starting", PopupPhase::Starting},
    {"listening", PopupPhase::Listening},
    {"paused", PopupPhase::Paused},
    {"transcribing", PopupPhase::Transcribing},
    {"refining", PopupPhase::Refining},
    {"delivering", PopupPhase::Delivering},
    {"delivered", PopupPhase::Delivered},
    {"review", PopupPhase::Review},
    {"error", PopupPhase::Error},
};

// The popup's phase for a session state.
PopupPhase phaseFor(DictationState state)
{
    switch (state) {
    case DictationState::Idle: return PopupPhase::Idle;
    case DictationState::Starting: return PopupPhase::Starting;
    case DictationState::Listening: return PopupPhase::Listening;
    case DictationState::Paused: return PopupPhase::Paused;
    case DictationState::Stopping: return PopupPhase::Transcribing;
    case DictationState::Refining: return PopupPhase::Refining;
    case DictationState::Delivering: return PopupPhase::Delivering;
    case DictationState::Reviewing: return PopupPhase::Review;
    case DictationState::Error: break;
    }
    return PopupPhase::Error;
}

QString outcomeName(PopupOutcome outcome)
{
    switch (outcome) {
    case PopupOutcome::Inserted: return QStringLiteral("inserted");
    case PopupOutcome::Copied: return QStringLiteral("copied");
    case PopupOutcome::Fallback: return QStringLiteral("fallback");
    case PopupOutcome::Cancelled: return QStringLiteral("cancelled");
    case PopupOutcome::Error: break;
    }
    return {};
}

// The page the popup's fix opens. A refused permission opens Permissions &
// health, whose row opens the system's own settings where it has them.
QString fixPage(const PopupErrorAction &fix)
{
    switch (fix.fix) {
    case ErrorFix::SettingsPage: return fix.pageId;
    case ErrorFix::MicrophonePermission:
    case ErrorFix::AccessibilityPermission: return QStringLiteral("permissions");
    case ErrorFix::None: break;
    }
    return {};
}

// A receipt or an error as the popup's title and detail: what happened, then
// the notes on it.
QVariantMap message(const QString &text, const PopupErrorAction &fix)
{
    const QStringList parts = text.split(popupMessageSeparator());
    return {{QStringLiteral("title"), parts.first()},
            {QStringLiteral("detail"), parts.mid(1).join(popupMessageSeparator())},
            {QStringLiteral("page"), fixPage(fix)}};
}

QVariantMap errorMessage(const QString &text, const PopupErrorAction &fix)
{
    QVariantMap error = message(text, fix);
    error.insert(QStringLiteral("dismissMs"), g_errorDismissMs > 0 ? g_errorDismissMs : popupErrorDismissMs(text));
    return error;
}

// Each run, and the spacing after it unmarked, so the marks on two
// neighbouring words never run together. An omitted run's text is its "…".
QVariantList segments(const QList<EditRun> &runs)
{
    const auto segment = [](const char *kind, const QString &text) {
        return QVariantMap{{QStringLiteral("kind"), QString::fromLatin1(kind)}, {QStringLiteral("text"), text}};
    };
    QVariantList segments;
    for (const EditRun &run : runs) {
        switch (run.kind) {
        case EditRun::Kind::Removed: segments.append(segment("removed", run.text)); break;
        case EditRun::Kind::Added: segments.append(segment("added", run.text)); break;
        case EditRun::Kind::Kept:
        case EditRun::Kind::Omitted: segments.append(segment("same", run.text)); break;
        }
        if (!run.trailing.isEmpty()) {
            segments.append(segment("same", run.trailing));
        }
    }
    return segments;
}

// The keys that answer a review, as the popup's lists of keycaps.
QVariantMap keyLists(const ReviewKeys &keys)
{
    return {{QStringLiteral("keepKeys"), shortcutKeys(keys.keep)},
            {QStringLiteral("replaceKeys"), shortcutKeys(keys.replace)},
            {QStringLiteral("followUpKeys"), shortcutKeys(keys.followUp)}};
}

} // namespace

template <typename T>
void QuickDictation::update(T &member, const T &value, void (QuickDictationState::*changed)())
{
    if (member == value) {
        return;
    }
    member = value;
    emit(this->*changed)();
}

QuickDictationState::QuickDictationState(QObject *parent)
    : QObject(parent)
{
}

QString QuickDictationState::state() const
{
    for (const PhaseName &name : phaseNames) {
        if (name.phase == m_phase) {
            return QLatin1String(name.id);
        }
    }
    return {};
}

void QuickDictationState::setState(const QString &name)
{
    const PhaseName *found = findRow(phaseNames, name);
    if (!found) {
        qWarning().noquote() << "DictationState: no popup phase is called" << name;
        return;
    }
    setPhase(found->phase);
}

void QuickDictationState::setPhase(PopupPhase phase)
{
    if (m_phase == phase) {
        return;
    }
    m_phase = phase;
    emit stateChanged();
}

QuickDictation::QuickDictation(ApplicationController *controller, QObject *parent)
    : QuickDictationState(parent)
    , m_controller(controller)
{
    // A session's error that says nothing still counts down.
    m_error = errorMessage({}, {});
    m_errorDismissal.setSingleShot(true);
    connect(&m_errorDismissal, &QTimer::timeout, this, &QuickDictation::dismissError);
    m_clock.setSingleShot(true);
    m_clock.setTimerType(Qt::PreciseTimer);
    connect(&m_clock, &QTimer::timeout, this, &QuickDictation::updateElapsed);

    DictationSession *session = controller->session();
    connect(session, &DictationSession::stateChanged, this, &QuickDictation::followSession);
    connect(session, &DictationSession::audioLevelChanged, this, [this](float level) {
        update(m_level, qreal(level), &QuickDictationState::levelChanged);
    });
    connect(session, &DictationSession::previewDisplayChanged, this, [this](const QString &words) {
        update(m_words, words, &QuickDictationState::wordsChanged);
    });
    connect(session, &DictationSession::popupRefinementPreviewChanged, this, [this](const QString &refined) {
        update(m_refined, refined, &QuickDictationState::refinedChanged);
    });
    connect(session, &DictationSession::popupOAuthRefreshRequested, this, [this] {
        update(m_renewing, true, &QuickDictationState::renewingChanged);
    });
    connect(session, &DictationSession::popupMessageRequested, this, &QuickDictation::showReceipt);
    connect(session, &DictationSession::popupErrorRequested, this, &QuickDictation::showError);
    connect(session, &DictationSession::popupSelectionEditReviewRequested, this, &QuickDictation::setReview);
    connect(session, &DictationSession::popupHideRequested, this, [this] { setPhase(PopupPhase::Idle); });
    connect(controller, &ApplicationController::globalShortcutChanged, this, &QuickDictation::showConfiguredReviewKeys);
    showConfiguredReviewKeys();

    connect(this, &QuickDictationState::startRequested, controller, &ApplicationController::startListening);
    connect(this, &QuickDictationState::stopRequested, controller, &ApplicationController::stopListening);
    connect(this, &QuickDictationState::pauseRequested, session, &DictationSession::togglePause);
    connect(this, &QuickDictationState::cancelRequested, this, &QuickDictation::cancel);
    connect(this, &QuickDictationState::replaceRequested, session, &DictationSession::replaceSelection);
    // In review the controller's toggle dictates a follow-up for the same
    // selection, as the Global Shortcut does.
    connect(this, &QuickDictationState::followUpRequested, controller, &ApplicationController::toggle);
    connect(this, &QuickDictationState::errorActionRequested, this, &QuickDictation::openFix);
    connect(this, &QuickDictationState::errorHoldRequested, this, &QuickDictation::holdError);
}

// Another error over one on show, even the same one again, counts down from
// the start; errorChanged always fires so the popup's bar starts over too.
void QuickDictation::showError(const QString &text, const PopupErrorAction &fix)
{
    const bool showing = m_phase == PopupPhase::Error;
    m_error = errorMessage(text, fix);
    emit errorChanged();
    if (showing) {
        startErrorDismissal();
    }
    setPhase(PopupPhase::Error);
}

void QuickDictation::setErrorDismissMs(int ms)
{
    g_errorDismissMs = ms;
}

void QuickDictation::followSession(DictationState state)
{
    if (state == DictationState::Starting) {
        // The last dictation's refinement and error are not this one's.
        update(m_refined, QString(), &QuickDictationState::refinedChanged);
        update(m_error, errorMessage({}, {}), &QuickDictationState::errorChanged);
    }
    // A cancel's receipt follows the session going idle, and a receipt or an
    // error stays up until the session hides the popup.
    if (state == DictationState::Idle && (m_phase == PopupPhase::Delivered || m_phase == PopupPhase::Error)) {
        return;
    }
    setPhase(phaseFor(state));
}

void QuickDictation::showReceipt(const QString &text, PopupOutcome outcome, const PopupErrorAction &fix)
{
    if (outcome == PopupOutcome::Error) {
        showError(text, fix);
        return;
    }
    QVariantMap receipt = message(text, fix);
    receipt.insert(QStringLiteral("outcome"), outcomeName(outcome));
    update(m_receipt, receipt, &QuickDictationState::receiptChanged);
    setPhase(PopupPhase::Delivered);
}

void QuickDictation::setReview(const SelectionEditReview &review)
{
    QVariantMap value{
        {QStringLiteral("instruction"), review.instruction},
        {QStringLiteral("whole"), segments(review.runs)},
        {QStringLiteral("folded"), segments(review.folded)},
        {QStringLiteral("summary"), review.summary},
        {QStringLiteral("following"), review.following},
    };
    value.insert(keyLists(review.keys));
    update(m_review, value, &QuickDictationState::reviewChanged);
}

// Between dictations the review carries the keys the configured shortcuts
// give one whose Escape and Enter are held, as they are once it is up, so
// the popup's window is already as wide as its buttons when it shows and
// does not widen, moving the capsule, when a review comes.
void QuickDictation::showConfiguredReviewKeys()
{
    if (m_phase != PopupPhase::Idle) {
        return;
    }
    QVariantMap value = m_review;
    value.insert(keyLists(reviewKeysFor(true, true, m_controller->globalShortcutDisplay(GlobalShortcutRole::Cancel),
                                        m_controller->globalShortcutDisplay())));
    update(m_review, value, &QuickDictationState::reviewChanged);
}

// Counts the error on show down from its full time, or keeps that time while
// the pointer rests on it.
void QuickDictation::startErrorDismissal()
{
    m_errorRemainingMs = m_error.value(QStringLiteral("dismissMs")).toInt();
    if (m_errorHeld) {
        m_errorDismissal.stop();
        return;
    }
    m_errorDismissal.start(m_errorRemainingMs);
}

void QuickDictation::holdError(bool held)
{
    if (std::exchange(m_errorHeld, held) == held || m_phase != PopupPhase::Error) {
        return;
    }
    if (held) {
        m_errorRemainingMs = m_errorDismissal.remainingTime();
        m_errorDismissal.stop();
        return;
    }
    m_errorDismissal.start(m_errorRemainingMs);
}

// As the session's own cancel does: it ends its error and hides the popup;
// one from outside the session just goes.
void QuickDictation::dismissError()
{
    DictationSession *session = m_controller->session();
    if (session->state() == DictationState::Error) {
        session->stopListening();
        return;
    }
    setPhase(PopupPhase::Idle);
}

// The fix leaves the popup for the page: an error is dismissed, a receipt
// goes before its time.
void QuickDictation::openFix()
{
    const bool error = m_phase == PopupPhase::Error;
    const QString page = (error ? m_error : m_receipt).value(QStringLiteral("page")).toString();
    if (page.isEmpty()) {
        return;
    }
    if (error) {
        dismissError();
    } else {
        setPhase(PopupPhase::Idle);
    }
    emit pageRequested(page);
}

void QuickDictation::cancel()
{
    if (m_phase == PopupPhase::Error) {
        dismissError();
        return;
    }
    m_controller->cancel();
}

// While listening, ticks again on the next whole second listened, wherever
// a pause left the count.
void QuickDictation::updateElapsed()
{
    const int listenedMs = m_controller->session()->listeningMs();
    update(m_elapsed, listenedMs / msPerSecond, &QuickDictationState::elapsedChanged);
    if (m_phase == PopupPhase::Listening) {
        m_clock.start(msPerSecond - listenedMs % msPerSecond);
    } else {
        m_clock.stop();
    }
}

void QuickDictation::setPhase(PopupPhase phase)
{
    const bool error = phase == PopupPhase::Error;
    // However the state turned to error, a session's error with no words too.
    if (error && m_phase != PopupPhase::Error) {
        startErrorDismissal();
    }
    if (!error) {
        m_errorDismissal.stop();
        m_errorHeld = false;
    }
    if (phase != m_phase) {
        update(m_renewing, false, &QuickDictationState::renewingChanged);
    }
    if (phase == PopupPhase::Idle) {
        update(m_level, qreal(0), &QuickDictationState::levelChanged);
    }
    update(m_phase, phase, &QuickDictationState::stateChanged);
    showConfiguredReviewKeys();
    updateElapsed();
}

} // namespace speecher
