#include "ui/DictationPage.h"

#include "app/LinuxComposition.h"
#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "providers/ProviderRegistry.h"
#include "ui/AccessibilityNotice.h"
#include "ui/WaveformWidget.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

namespace speecher {

namespace {

void addSummaryRow(QFormLayout *form,
                   const QString &labelText,
                   QLabel *value,
                   AppPageId page,
                   DictationPage *owner,
                   QWidget *parent)
{
    auto *change = new QPushButton(QStringLiteral("Change…"), parent);
    auto *field = new QWidget(parent);
    auto *layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(settings::relatedSpacing());
    layout->addWidget(value, 0, Qt::AlignVCenter);
    layout->addWidget(change, 0, Qt::AlignVCenter);
    form->addRow(labelText + QLatin1Char(':'), field);
    QObject::connect(change, &QPushButton::clicked, owner, [owner, page] {
        emit owner->navigateRequested(page);
    });
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
    , m_microphone(new QLabel(this))
    , m_output(new QLabel(this))
    , m_theme(new QLabel(this))
{
    auto *pageLayout = new QVBoxLayout(this);
    settings::applyPageMargins(pageLayout);
    pageLayout->setSpacing(settings::relatedSpacing());
    m_toggle->setMinimumWidth(0);
    m_toggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    pageLayout->addWidget(m_toggle, 0, Qt::AlignHCenter);

    auto *formWidget = new QWidget(this);
    auto *form = new QFormLayout(formWidget);
    settings::configureFormLayout(form);

    m_accessibilityNotice->setCompact(true);
    form->addRow(QString(), m_accessibilityNotice);

    auto *statusField = new QWidget(formWidget);
    auto *statusLayout = new QVBoxLayout(statusField);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(settings::tightSpacing());
    m_status->setForegroundRole(QPalette::WindowText);
    statusLayout->addWidget(m_status, 0, Qt::AlignLeft);
    statusLayout->addWidget(m_waveform, 0, Qt::AlignLeft);
    form->addRow(QStringLiteral("Status:"), statusField);

    const int valueWidth = fontMetrics().horizontalAdvance(QString(40, QLatin1Char('x')));
    for (QLabel *label : {m_provider, m_microphone, m_output, m_theme}) {
        label->setMaximumWidth(valueWidth);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        label->installEventFilter(this);
    }
    addSummaryRow(form, QStringLiteral("Refinement"), m_provider,
                  AppPageId::Refinement, this, formWidget);
    addSummaryRow(form, QStringLiteral("Microphone"), m_microphone,
                  AppPageId::Audio, this, formWidget);
    addSummaryRow(form, QStringLiteral("Output"), m_output,
                  AppPageId::Output, this, formWidget);
    addSummaryRow(form, QStringLiteral("Theme"), m_theme,
                  AppPageId::General, this, formWidget);
    pageLayout->addWidget(formWidget);
    pageLayout->addStretch();

    connect(m_toggle, &QPushButton::clicked, controller, &ApplicationController::toggle);
    connect(controller, &ApplicationController::statusChanged, this, &DictationPage::setStatus);
    connect(controller, &ApplicationController::audioLevelChanged, m_waveform, &WaveformWidget::setLevel);
    connect(m_accessibilityNotice, &AccessibilityNotice::enableRequested, this, [this] {
        QString error;
        if (!m_controller->enableAccessibility(&error)) {
            m_accessibilityNotice->showError(error);
        }
    });
    connect(controller, &ApplicationController::accessibilityStateChanged, this,
            [this](bool supported, bool enabled, bool persistent) {
                if (!supported) {
                    m_accessibilityNotice->hide();
                } else {
                    m_accessibilityNotice->setState(supported, enabled, persistent);
                }
            });
    if (controller->accessibilitySupported()) {
        m_accessibilityNotice->setState(true,
                                        controller->accessibilityEnabled(),
                                        controller->accessibilityPersistent());
    } else {
        m_accessibilityNotice->hide();
    }
    setStatus(controller->stateName());
    refreshSummary();
}

QPushButton *DictationPage::toggleButton() const
{
    return m_toggle;
}

void DictationPage::setStatus(const QString &status)
{
    const QString state = status.toCaseFolded();
    const bool active = state == QStringLiteral("starting") || state == QStringLiteral("listening");
    m_toggle->setText(active ? QStringLiteral("Stop Dictation")
                             : QStringLiteral("Start Dictation"));
    m_toggle->setIcon(QIcon::fromTheme(active ? QStringLiteral("media-playback-stop")
                                               : QStringLiteral("media-record")));
    static const QStringList states{
        QStringLiteral("idle"),
        QStringLiteral("starting"),
        QStringLiteral("listening"),
        QStringLiteral("stopping"),
        QStringLiteral("refining"),
        QStringLiteral("delivering"),
        QStringLiteral("error"),
    };
    m_status->setText(states.contains(state)
                          ? state.left(1).toUpper() + state.mid(1)
                          : status);
    m_status->setForegroundRole(QPalette::WindowText);
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
    setSummaryText(m_provider, providerName);
    QString microphone = QStringLiteral("System default");
    const QString deviceId = m_controller->settings()->audioInputDeviceId();
    if (!deviceId.isEmpty()) {
        for (const AudioInputDeviceInfo &device : m_controller->platform()->availableAudioInputDevices()) {
            if (device.id == deviceId) {
                microphone = device.label;
                break;
            }
        }
    }
    setSummaryText(m_microphone, microphone);
    setSummaryText(m_output, m_controller->primaryOutputStatus());
    const QString theme = m_controller->settings()->theme();
    setSummaryText(m_theme, theme.left(1).toUpper() + theme.mid(1));
}

bool DictationPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize) {
        if (auto *label = qobject_cast<QLabel *>(watched)) {
            const QString fullText = label->property("fullText").toString();
            if (!fullText.isEmpty()) {
                label->setText(label->fontMetrics().elidedText(fullText,
                                                                Qt::ElideRight,
                                                                label->maximumWidth()));
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DictationPage::setSummaryText(QLabel *label, const QString &text)
{
    label->setProperty("fullText", text);
    label->setToolTip(text);
    label->setText(label->fontMetrics().elidedText(text, Qt::ElideRight, label->maximumWidth()));
}

} // namespace speecher
