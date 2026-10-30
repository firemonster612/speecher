#include "frontend/quick/host/QuickRemoval.h"

#include "app/VirtualKeyboardSetup.h"

#include <QDebug>

#include <utility>

namespace speecher {

namespace {

QString partName(RemovalStep::Part part)
{
    switch (part) {
    case RemovalStep::Part::ShortcutRegistration:
        return QStringLiteral("shortcutRegistration");
    case RemovalStep::Part::VirtualKeyboardService:
        return QStringLiteral("virtualKeyboardService");
    case RemovalStep::Part::VirtualKeyboard:
        return QStringLiteral("virtualKeyboard");
    case RemovalStep::Part::KeyHelper:
        return QStringLiteral("keyHelper");
    case RemovalStep::Part::Keyring:
        return QStringLiteral("keyring");
    case RemovalStep::Part::Settings:
        break;
    }
    return QStringLiteral("settings");
}

QString outcomeName(RemovalStep::Outcome outcome)
{
    switch (outcome) {
    case RemovalStep::Outcome::Removed:
        return QStringLiteral("removed");
    case RemovalStep::Outcome::Failed:
        return QStringLiteral("failed");
    case RemovalStep::Outcome::KeptHidingKeys:
        return QStringLiteral("keptHidingKeys");
    case RemovalStep::Outcome::Kept:
        break;
    }
    return QStringLiteral("kept");
}

QVariantMap reportMap(const RemovalReport &report)
{
    QVariantList steps;
    for (const RemovalStep &step : report.steps) {
        steps.append(QVariantMap{{QStringLiteral("part"), partName(step.part)},
                                 {QStringLiteral("outcome"), outcomeName(step.outcome)},
                                 {QStringLiteral("error"), step.error}});
    }
    return {{QStringLiteral("files"), QVariantMap{{QStringLiteral("removed"), report.files.removed},
                                                  {QStringLiteral("absent"), report.files.absent},
                                                  {QStringLiteral("failed"), report.files.failed}}},
            {QStringLiteral("steps"), steps},
            {QStringLiteral("settingsDeleted"), report.settingsDeleted()},
            {QStringLiteral("programFile"), report.programFile}};
}

} // namespace

QuickRemoval::QuickRemoval(VirtualKeyboardSetup *virtualKeyboard, std::optional<SpeecherRemoval::Steps> steps,
                           QObject *parent)
    : QObject(parent)
    , m_virtualKeyboard(virtualKeyboard)
{
    if (!steps) {
        return;
    }
    m_removal = std::make_unique<SpeecherRemoval>(std::move(*steps));
    connect(m_virtualKeyboard, &VirtualKeyboardSetup::changed, this, &QuickRemoval::startAsked);
    connect(m_removal.get(), &SpeecherRemoval::finished, this, [this](const RemovalReport &report) {
        m_report = reportMap(report);
        emit changed();
    });
}

QuickRemoval::~QuickRemoval() = default;

bool QuickRemoval::running() const
{
    return m_asked || (m_removal && m_removal->running());
}

void QuickRemoval::start(bool keepSettings)
{
    if (!m_removal) {
        qWarning().noquote() << "Removal.start: Speecher can't remove itself from this computer";
        return;
    }
    if (running()) {
        return;
    }
    m_asked = keepSettings ? Asked::KeepSettings : Asked::DeleteSettings;
    startAsked();
    emit changed();
}

void QuickRemoval::startAsked()
{
    // The keyboard's setup saves settings and runs the helper removal runs
    // too, so removal waits for an operation under way and ends the test.
    if (!m_asked || m_virtualKeyboard->operation() != VirtualKeyboardSetup::Operation::None) {
        return;
    }
    const bool deleteSettings = *std::exchange(m_asked, std::nullopt) == Asked::DeleteSettings;
    m_virtualKeyboard->cancelTest();
    m_removal->start(deleteSettings);
}

} // namespace speecher
