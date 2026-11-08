#include "app/PasteAgain.h"

#include "core/Apps.h"
#include "core/SettingsStore.h"
#include "dictation/DictationPorts.h"

#include <QClipboard>
#include <QGuiApplication>

namespace speecher {
namespace {

constexpr int pollMs = 100;
// After Speecher lets go of focus, the app behind takes a moment to have it.
constexpr int settleMs = 300;

} // namespace

PasteAgain::PasteAgain(SettingsStore &settings, TargetProvider *targets, TextDeliveryAdapter *delivery,
                       QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_targets(targets)
    , m_delivery(delivery)
{
    m_waitForFocus.setInterval(pollMs);
    connect(&m_waitForFocus, &QTimer::timeout, this, [this] {
        m_waitedMs += pollMs;
        if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
            m_waitForFocus.stop();
            QTimer::singleShot(settleMs, this, [this, generation = m_generation] {
                if (generation == m_generation) {
                    captureAndDeliver();
                }
            });
        } else if (m_waitedMs >= focusWaitMs) {
            m_waitForFocus.stop();
            copyInstead();
        }
    });
    connect(m_delivery, &TextDeliveryAdapter::delivered, this, [this](const DeliveryResult &result) {
        emit finished(result.ok, result.message);
    });
}

void PasteAgain::paste(const QString &text)
{
    ++m_generation;
    m_text = text;
    m_waitedMs = 0;
    if (QGuiApplication::applicationState() == Qt::ApplicationActive) {
        m_waitForFocus.start();
        return;
    }
    captureAndDeliver();
}

void PasteAgain::captureAndDeliver()
{
    if (!m_targets) {
        copyInstead();
        return;
    }
    m_targets->capture(this, [this, generation = m_generation](const Target &target) {
        if (generation != m_generation) {
            return;
        }
        if (!target.hasIdentity()) {
            copyInstead();
            return;
        }
        const AppSettings settings = m_settings.dictationSnapshot();
        m_delivery->deliver(settings.output, makeDeliveryContent(m_text, settings.output.format), target,
                            resolveApp(target, settings.apps).pasting);
    });
}

void PasteAgain::copyInstead()
{
    QGuiApplication::clipboard()->setText(m_text);
    emit finished(false, QStringLiteral("Couldn't tell which app to paste into, so the words are on the clipboard."));
}

} // namespace speecher
