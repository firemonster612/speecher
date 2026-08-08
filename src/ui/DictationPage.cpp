#include "ui/DictationPage.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "providers/ProviderRegistry.h"
#include "ui/AccessibilityNotice.h"
#include "ui/WaveformWidget.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace speecher {

namespace {

QWidget *summaryRow(QLabel *value, const QString &buttonText, int page,
                    DictationPage *owner, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(value, 1);
    auto *change = new QPushButton(buttonText, row);
    layout->addWidget(change);
    QObject::connect(change, &QPushButton::clicked, owner, [owner, page] {
        emit owner->navigateRequested(page);
    });
    return row;
}

} // namespace

DictationPage::DictationPage(ApplicationController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_accessibilityNotice(new AccessibilityNotice(this))
    , m_toggle(new QPushButton(this))
    , m_status(new QLabel(this))
    , m_waveform(new WaveformWidget(this))
    , m_provider(new QLabel(this))
    , m_output(new QLabel(this))
    , m_theme(new QLabel(this))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
    m_accessibilityNotice->setCompact(true);
    layout->addWidget(m_accessibilityNotice);

    m_toggle->setMinimumHeight(56);
    layout->addWidget(m_toggle);
    m_status->setAlignment(Qt::AlignHCenter);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_status);
    layout->addWidget(m_waveform, 0, Qt::AlignHCenter);

    auto *summary = new QGroupBox(QStringLiteral("At a glance"), this);
    auto *form = new QFormLayout(summary);
    form->addRow(QStringLiteral("Refinement"),
                 summaryRow(m_provider, QStringLiteral("Change…"), 4, this, summary));
    form->addRow(QStringLiteral("Output"),
                 summaryRow(m_output, QStringLiteral("Change…"), 3, this, summary));
    form->addRow(QStringLiteral("Theme"),
                 summaryRow(m_theme, QStringLiteral("Change…"), 0, this, summary));
    layout->addWidget(summary);
    layout->addStretch();

    connect(m_toggle, &QPushButton::clicked, controller, &ApplicationController::toggle);
    connect(controller, &ApplicationController::statusChanged, this, &DictationPage::setStatus);
    connect(controller, &ApplicationController::audioLevelChanged, m_waveform, &WaveformWidget::setLevel);
    connect(m_accessibilityNotice, &AccessibilityNotice::enableRequested, this, [this] {
        QString error;
        if (!m_controller->enableAccessibility(&error)) {
            m_accessibilityNotice->showError(error);
        }
    });
    connect(controller,
            &ApplicationController::accessibilityStateChanged,
            m_accessibilityNotice,
            qOverload<bool, bool, bool>(&AccessibilityNotice::setState));
    m_accessibilityNotice->setState(controller->accessibilitySupported(),
                                    controller->accessibilityEnabled(),
                                    controller->accessibilityPersistent());
    setStatus(controller->stateName());
    refreshSummary();
}

void DictationPage::setStatus(const QString &status)
{
    const QString state = status.toCaseFolded();
    const bool active = state == QStringLiteral("starting") || state == QStringLiteral("listening");
    m_toggle->setText(active ? QStringLiteral("Stop") : QStringLiteral("Start"));
    m_status->setText(status);
    m_waveform->setVisible(active);
    if (!active) {
        m_waveform->setLevel(0.0f);
    }
}

void DictationPage::refreshSummary()
{
    const QString providerId = m_controller->settings()->refinementProvider();
    QString providerName = QStringLiteral("None");
    for (const ProviderDescriptor &provider : m_controller->providerRegistry()->refinementProviders()) {
        if (provider.id == providerId) {
            providerName = provider.label;
            break;
        }
    }
    m_provider->setText(providerName);
    m_output->setText(m_controller->primaryOutputStatus());
    const QString theme = m_controller->settings()->theme();
    m_theme->setText(theme.left(1).toUpper() + theme.mid(1));
}

} // namespace speecher
