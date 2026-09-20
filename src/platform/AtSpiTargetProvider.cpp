#include "platform/AtSpiTargetProvider.h"

#include "platform/KWinActiveWindow.h"
#include "platform/atspi/AtSpiAccess.h"
#include "platform/atspi/AtSpiCorrectionObserver.h"
#include "platform/atspi/AtSpiTargetSnapshot.h"

#include <QFile>
#include <QThread>

namespace speecher {

namespace {

QString processNameForPid(qint64 pid)
{
    if (pid <= 0) {
        return {};
    }
    QFile comm(QStringLiteral("/proc/%1/comm").arg(pid));
    if (!comm.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(comm.readAll()).trimmed();
}

} // namespace

AtSpiTargetProvider::AtSpiTargetProvider(QObject *parent)
    : TargetProvider(parent)
{
}

AtSpiTargetProvider::~AtSpiTargetProvider()
{
    clearAccessible();
}

void AtSpiTargetProvider::clearAccessible()
{
    if (m_correctionObserver) m_correctionObserver->cancel();
    m_snapshot.reset();
}

Target AtSpiTargetProvider::capture(const QList<AppRecognitionRule> &recognitionRules)
{
    clearAccessible();
    m_snapshot = std::make_unique<atspi::TargetSnapshot>(atspi::TargetSnapshot::capture());
    Target target = m_snapshot->target();
    target.category = classifyTarget(target, recognitionRules);
    if (!target.hasIdentity()) {
        Target fromCompositor = compositorFallbackTarget(recognitionRules);
        if (fromCompositor.hasIdentity()) {
            return fromCompositor;
        }
    }
    return target;
}

// Accessibility found no focus target. Ask the compositor which window is
// active so a terminal that never reached the a11y bus still resolves to the
// terminal paste rule instead of the global blind-paste chord.
Target AtSpiTargetProvider::compositorFallbackTarget(
    const QList<AppRecognitionRule> &recognitionRules)
{
    if (!m_activeWindow) {
        m_activeWindow = std::make_unique<KWinActiveWindow>();
    }
    const std::optional<KWinWindowInfo> window = m_activeWindow->activeWindow();
    if (!window) {
        return {};
    }

    Target target;
    target.applicationId = window->resourceClass.toCaseFolded();
    target.applicationName = window->caption;
    target.processName = !window->resourceName.isEmpty()
        ? window->resourceName
        : processNameForPid(window->processId);
    target.processId = window->processId;
    target.compositorWindowId = window->internalId;
    target.compositorActive = true;
    target.category = classifyTarget(target, recognitionRules);
    target.terminalHost = isTerminalTarget(target);
    return target;
}

// KWin's window handle answers this outright where it exists: it survives the
// retitles that terminals and browsers do mid-dictation, and it tells two
// windows of one program apart, which the process and class cannot.
//
// Without it, the process and class alone would accept the second window of
// the same program, and the caption alone would reject a retitled one. Both
// have to match, and a caption-only mismatch is reported as its own answer so
// delivery can stay quiet about which of the two it was.
TargetProvider::FocusMatch AtSpiTargetProvider::compositorWindowMatch(const Target &a,
                                                                      const Target &b)
{
    if (!a.compositorWindowId.isEmpty() && !b.compositorWindowId.isEmpty()) {
        return a.compositorWindowId == b.compositorWindowId ? FocusMatch::Same
                                                            : FocusMatch::Different;
    }
    if (a.applicationId != b.applicationId || a.processId != b.processId) {
        return FocusMatch::Different;
    }
    // applicationName carries the caption for a compositor-fallback target.
    return a.applicationName == b.applicationName ? FocusMatch::Same : FocusMatch::TitleChanged;
}

TargetProvider::FocusMatch AtSpiTargetProvider::focusMatch(const Target &target)
{
    if (!target.compositorActive) {
        return m_snapshot && m_snapshot->matches(target, true) ? FocusMatch::Same
                                                               : FocusMatch::Different;
    }
    const Target active = compositorFallbackTarget({});
    if (!active.compositorActive) {
        return FocusMatch::Different;
    }
    return compositorWindowMatch(active, target);
}

bool AtSpiTargetProvider::stillFocused(const Target &target)
{
    return focusMatch(target) == FocusMatch::Same;
}

bool AtSpiTargetProvider::canInsertText(const Target &target)
{
    return m_snapshot && m_snapshot->canInsert(target);
}

bool AtSpiTargetProvider::insertText(const Target &target, const QString &plainText, QString *error)
{
    if (!m_snapshot) {
#ifdef SPEECHER_WITH_ATSPI
        if (error) *error = QStringLiteral("The saved accessible target is no longer safe to edit");
#else
        if (error) *error = QStringLiteral("AT-SPI support is not available in this build");
#endif
        return false;
    }
    return m_snapshot->insert(target, plainText, error);
}

bool AtSpiTargetProvider::verifyInsertion(const Target &target, const QString &plainText)
{
    if (!m_snapshot || !m_snapshot->valid() || target.secure
        || target.fingerprint != m_snapshot->target().fingerprint
        || plainText.isEmpty()) {
        return false;
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (attempt > 0) QThread::msleep(40);
        const auto window = m_snapshot->verifiedInsertion(plainText);
        if (!window) continue;
        const QString &prefix = window->prefix;
        const QString &suffix = window->suffix;
        if (m_correctionObservationEnabled && prefix.size() >= correctionMinContextChars
            && suffix.size() >= correctionMinContextChars) {
            if (!m_correctionObserver) {
                m_correctionObserver = std::make_unique<atspi::CorrectionObserver>();
            }
            m_correctionObserver->schedule(
                this, m_snapshot.get(), *window,
                [this](const QString &original, const QString &corrected,
                       const QString &applicationId, double confidence) {
                    emit correctionObserved(original, corrected, applicationId, confidence);
                });
        }
        return true;
    }
    return false;
}

void AtSpiTargetProvider::setCorrectionObservationEnabled(bool enabled)
{
    m_correctionObservationEnabled = enabled;
    if (m_correctionObserver) {
        m_correctionObserver->setEnabled(enabled);
    }
}

} // namespace speecher
