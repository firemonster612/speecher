#include "frontend/quick/host/QuickDictation.h"

#include "app/ApplicationController.h"
#include "dictation/DictationSession.h"
#include "dictation/SelectionEditPresentation.h"
#include "frontend/quick/host/QuickSettings.h"

namespace speecher {

namespace {

QuickDictation *g_instance = nullptr;
constexpr int clockTickMs = 1000;

// The popup's phase for a session state name.
QString phaseFor(const QString &stateName)
{
    if (stateName == QLatin1String("stopping")) {
        return QStringLiteral("transcribing");
    }
    if (stateName == QLatin1String("reviewing")) {
        return QStringLiteral("review");
    }
    return stateName;
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

// Linux has no system permission panels to open, so only a settings page is
// a fix the popup offers.
QString fixPage(const PopupErrorAction &fix)
{
    return fix.fix == ErrorFix::SettingsPage ? fix.pageId : QString();
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

QuickDictation::QuickDictation(ApplicationController *controller, QObject *parent)
    : QuickDictationState(parent)
    , m_controller(controller)
{
    g_instance = this;
    m_errorDismissal.setSingleShot(true);
    connect(&m_errorDismissal, &QTimer::timeout, this, &QuickDictation::dismissError);
    m_clock.setInterval(clockTickMs);
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
    connect(session, &DictationSession::popupMessageRequested, this, &QuickDictation::showReceipt);
    connect(session, &DictationSession::popupErrorRequested, this, &QuickDictation::setError);
    connect(session, &DictationSession::popupSelectionEditReviewRequested, this, &QuickDictation::setReview);
    connect(session, &DictationSession::popupHideRequested, this, [this] { setState(QStringLiteral("idle")); });

    connect(this, &QuickDictationState::startRequested, controller, &ApplicationController::startListening);
    connect(this, &QuickDictationState::stopRequested, controller, &ApplicationController::stopListening);
    connect(this, &QuickDictationState::pauseRequested, session, &DictationSession::togglePause);
    connect(this, &QuickDictationState::cancelRequested, this, &QuickDictation::cancel);
    connect(this, &QuickDictationState::replaceRequested, session, &DictationSession::replaceSelection);
    // In review the controller's toggle dictates a follow-up for the same
    // selection, as the Global Shortcut does.
    connect(this, &QuickDictationState::followUpRequested, controller, &ApplicationController::toggle);
    connect(this, &QuickDictationState::errorActionRequested, this, &QuickDictation::openFix);
}

QuickDictation::~QuickDictation()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QuickDictation *QuickDictation::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(g_instance, "QuickDictation::create", "the host makes Dictation before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

void QuickDictation::showError(const QString &text, const PopupErrorAction &fix)
{
    setError(text, fix);
}

void QuickDictation::followSession(const QString &stateName)
{
    const QString phase = phaseFor(stateName);
    if (phase == QLatin1String("starting")) {
        // The last dictation's refinement is not this one's.
        update(m_refined, QString(), &QuickDictationState::refinedChanged);
    }
    updateElapsed();
    // A cancel's receipt follows the session going idle, and a receipt or an
    // error stays up until the session hides the popup.
    if (phase == QLatin1String("idle") && (m_state == QLatin1String("delivered") || m_state == QLatin1String("error"))) {
        return;
    }
    setState(phase);
}

void QuickDictation::showReceipt(const QString &text, PopupOutcome outcome, const PopupErrorAction &fix)
{
    if (outcome == PopupOutcome::Error) {
        setError(text, fix);
        return;
    }
    QVariantMap receipt = message(text, fix);
    receipt.insert(QStringLiteral("outcome"), outcomeName(outcome));
    update(m_receipt, receipt, &QuickDictationState::receiptChanged);
    setState(QStringLiteral("delivered"));
}

void QuickDictation::setError(const QString &text, const PopupErrorAction &fix)
{
    update(m_error, message(text, fix), &QuickDictationState::errorChanged);
    setState(QStringLiteral("error"));
    m_errorDismissal.start(popupErrorDismissMs(text));
}

void QuickDictation::setReview(const SelectionEditReview &review)
{
    const QVariantMap value{
        {QStringLiteral("instruction"), review.instruction},
        {QStringLiteral("whole"), segments(review.runs)},
        {QStringLiteral("folded"), segments(review.folded)},
        {QStringLiteral("summary"), review.summary},
        {QStringLiteral("keepKeys"), shortcutKeys(review.keys.keep)},
        {QStringLiteral("replaceKeys"), shortcutKeys(review.keys.replace)},
        {QStringLiteral("followUpKeys"), shortcutKeys(review.keys.followUp)},
        {QStringLiteral("following"), review.following},
    };
    update(m_review, value, &QuickDictationState::reviewChanged);
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
    setState(QStringLiteral("idle"));
}

// The fix leaves the popup for the page: an error is dismissed, a receipt
// goes before its time.
void QuickDictation::openFix()
{
    const bool error = m_state == QLatin1String("error");
    const QString page = (error ? m_error : m_receipt).value(QStringLiteral("page")).toString();
    if (page.isEmpty()) {
        return;
    }
    if (error) {
        dismissError();
    } else {
        setState(QStringLiteral("idle"));
    }
    emit pageRequested(page);
}

void QuickDictation::cancel()
{
    if (m_state == QLatin1String("error")) {
        dismissError();
        return;
    }
    m_controller->cancel();
}

void QuickDictation::updateElapsed()
{
    update(m_elapsed, m_controller->session()->listeningMs() / 1000, &QuickDictationState::elapsedChanged);
}

void QuickDictation::setState(const QString &state)
{
    if (state != QLatin1String("error")) {
        m_errorDismissal.stop();
    }
    if (state == QLatin1String("listening")) {
        m_clock.start();
    } else {
        m_clock.stop();
    }
    if (state == QLatin1String("idle")) {
        update(m_level, qreal(0), &QuickDictationState::levelChanged);
    }
    update(m_state, state, &QuickDictationState::stateChanged);
}

} // namespace speecher
