#include "platform/AtSpiTargetProvider.h"

#include "platform/atspi/AtSpiAccess.h"
#include "platform/atspi/AtSpiCorrectionObserver.h"
#include "platform/atspi/AtSpiTargetSnapshot.h"

#include <QThread>

namespace speecher {

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
    return target;
}

bool AtSpiTargetProvider::stillFocused(const Target &target)
{
    return m_snapshot && m_snapshot->matches(target, true);
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
    int insertionOffset = target.selectionStart >= 0 ? target.selectionStart : target.caretOffset;
    if (insertionOffset < 0) return false;

    for (int attempt = 0; attempt < 5; ++attempt) {
        if (attempt > 0) QThread::msleep(40);
        const QString nearby = m_snapshot->insertionWindow(insertionOffset, plainText.size());
        const int insertedAt = nearby.indexOf(plainText);
        if (insertedAt < 0) continue;
        const QString prefix = nearby.left(insertedAt).right(correctionContextChars);
        const QString suffix = nearby.mid(insertedAt + plainText.size()).left(correctionContextChars);
        if (m_correctionObservationEnabled && prefix.size() >= correctionMinContextChars
            && suffix.size() >= correctionMinContextChars) {
            if (!m_correctionObserver) {
                m_correctionObserver = std::make_unique<atspi::CorrectionObserver>();
            }
            m_correctionObserver->schedule(
                this, m_snapshot.get(), {target, plainText, prefix, suffix},
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
