#include "frontend/quick/host/QuickPhone.h"

#include "app/ApplicationController.h"
#include "app/PhoneTransfer.h"
#include "core/SettingsStore.h"

namespace speecher {

namespace {

QuickPhone *g_instance = nullptr;

QString stateName(PhoneTransferState state)
{
    switch (state) {
    case PhoneTransferState::Sent:
        return QStringLiteral("sent");
    case PhoneTransferState::Interrupted:
        return QStringLiteral("interrupted");
    case PhoneTransferState::NoNetwork:
        return QStringLiteral("noNetwork");
    case PhoneTransferState::Failed:
        return QStringLiteral("failed");
    case PhoneTransferState::Waiting:
        break;
    }
    return QStringLiteral("waiting");
}

} // namespace

QuickPhone::QuickPhone(ApplicationController *controller, QObject *parent)
    : QObject(parent)
    , m_controller(controller)
{
    g_instance = this;
}

QuickPhone::~QuickPhone()
{
    if (g_instance == this) {
        g_instance = nullptr;
    }
}

QuickPhone *QuickPhone::create(QQmlEngine *, QJSEngine *)
{
    Q_ASSERT_X(g_instance, "QuickPhone::create", "the host makes Phone before loading QML");
    QJSEngine::setObjectOwnership(g_instance, QJSEngine::CppOwnership);
    return g_instance;
}

QString QuickPhone::state() const
{
    return m_transfer ? stateName(m_transfer->state()) : QString();
}

QString QuickPhone::link() const
{
    return m_transfer ? m_transfer->link() : QString();
}

void QuickPhone::start()
{
    // A new transfer before the old one goes, so the old code stops working.
    const AppSettings settings = m_controller->settings()->dictationSnapshot();
    m_transfer = std::make_unique<PhoneTransfer>(settings);
    m_bundle = {{QStringLiteral("terms"), settings.vocabulary.size()},
                {QStringLiteral("replacements"), settings.bindings.size()},
                {QStringLiteral("profiles"), settings.refinement.writingProfiles.size()}};
    connect(m_transfer.get(), &PhoneTransfer::stateChanged, this, &QuickPhone::changed);
    emit changed();
}

void QuickPhone::stop()
{
    if (!m_transfer) {
        return;
    }
    m_transfer.reset();
    emit changed();
}

} // namespace speecher
