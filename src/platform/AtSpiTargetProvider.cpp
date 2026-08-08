#include "platform/AtSpiTargetProvider.h"

#include "output/WlClipboardDelivery.h"
#include "output/YdotoolDelivery.h"
#include "platform/atspi/AtSpiAccess.h"
#include "platform/atspi/AtSpiCorrectionObserver.h"
#include "platform/atspi/AtSpiTargetSnapshot.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QThread>
#include <QUuid>

namespace speecher {

namespace {

QString copiedSelection(const Target &target)
{
    if (!WlClipboardDelivery::canSnapshot() || !YdotoolDelivery::isAvailable()) {
        return {};
    }

    WlClipboardSnapshot previous;
    if (!WlClipboardDelivery::capture(&previous)) {
        return {};
    }

    const QString marker = QUuid::createUuid().toString(QUuid::WithoutBraces);
    WlClipboardDelivery clipboard;
    const PasteMethod copyMethod = isTerminalTarget(target)
        ? PasteMethod::TerminalPaste
        : PasteMethod::StandardPaste;
    if (!clipboard.copy({marker, std::nullopt})
        || !YdotoolDelivery().copySelection(copyMethod)) {
        WlClipboardDelivery::restore(previous);
        return {};
    }

    QString selectedText;
    for (int attempt = 0; attempt < 5; ++attempt) {
        if (attempt > 0) {
            QThread::msleep(20);
        }
        QString text;
        if (WlClipboardDelivery::readText(&text) && !text.isEmpty() && text != marker) {
            selectedText = text;
            break;
        }
    }
    WlClipboardDelivery::restore(previous);
    return selectedText;
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

Target AtSpiTargetProvider::capture()
{
    clearAccessible();
    m_snapshot = std::make_unique<atspi::TargetSnapshot>(atspi::TargetSnapshot::capture());
    Target target = m_snapshot->target();
    if (!target.hasSelection() && !target.secure) {
        target.selectedText = copiedSelection(target);
        if (!target.selectedText.isEmpty()) {
            target.selectionStart = 0;
            target.selectionEnd = target.selectedText.size();
        }
    }
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

    for (int attempt = 0; attempt < 9; ++attempt) {
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        if (attempt > 0) QThread::msleep(50);
        if (attempt == 3) {
            const Target current = capture();
            const bool sameProcess = target.processId > 0 && current.processId == target.processId;
            const bool sameApplication = !target.applicationId.isEmpty()
                && current.applicationId.compare(target.applicationId, Qt::CaseInsensitive) == 0;
            if (current.secure || (!sameProcess && !sameApplication)) return false;
            if (current.caretOffset >= plainText.size()) insertionOffset = current.caretOffset - plainText.size();
        }

        const QString nearby = m_snapshot->insertionWindow(insertionOffset, plainText.size());
        const int insertedAt = nearby.indexOf(plainText);
        if (insertedAt < 0) continue;
        const QString prefix = nearby.left(insertedAt).right(24);
        const QString suffix = nearby.mid(insertedAt + plainText.size()).left(24);
        if (m_correctionObservationEnabled && prefix.size() >= 8 && suffix.size() >= 8) {
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
