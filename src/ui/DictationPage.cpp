#include "ui/DictationPage.h"

#include "app/PlatformComposition.h"
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
                   const QString &description,
                   QLabel *value,
                   AppPageId page,
                   DictationPage *owner,
                   QWidget *parent,
                   bool addSeparator = true)
{
    auto *change = new QPushButton(QStringLiteral("Change…"), parent);
    auto *field = new QWidget(parent);
    auto *layout = new QHBoxLayout(field);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(settings::relatedSpacing());
    layout->addWidget(value, 0, Qt::AlignVCenter);
    layout->addWidget(change, 0, Qt::AlignVCenter);
    settings::addRow(form,
                     settings::makeRow(labelText, description, field, parent),
                     parent,
                     addSeparator);
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
    pageLayout->setSpacing(0);

    auto *river = new QWidget(this);
    river->setObjectName(QStringLiteral("settingsRiver"));
    river->setMaximumWidth(560);
    river->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *riverLayout = new QVBoxLayout(river);
    riverLayout->setContentsMargins(0, 0, 0, 0);
    riverLayout->setSpacing(0);

    riverLayout->addWidget(settings::makeSectionLabel(QStringLiteral("Status"), river));
    riverLayout->addSpacing(settings::tightSpacing());
    auto *statusCard = settings::makeSettingsCard(river);
    statusCard->setProperty("accentCard", true);
    auto *statusCardLayout = qobject_cast<QFormLayout *>(statusCard->layout());
    auto *statusField = new QWidget(statusCard);
    auto *statusLayout = new QVBoxLayout(statusField);
    statusLayout->setContentsMargins(18, 18, 18, 20);
    statusLayout->setSpacing(settings::relatedSpacing());

    m_status->setAlignment(Qt::AlignCenter);
    m_status->setObjectName(QStringLiteral("dictationStatus"));
    QFont statusFont = m_status->font();
    statusFont.setBold(true);
    m_status->setFont(statusFont);
    m_status->setForegroundRole(QPalette::Highlight);
    statusLayout->addWidget(m_status);
    statusLayout->addWidget(m_waveform, 0, Qt::AlignHCenter);
    m_toggle->setMinimumSize(160, 38);
    m_toggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_toggle->setObjectName(QStringLiteral("dictationToggle"));
    m_toggle->setDefault(true);
    statusLayout->addWidget(m_toggle, 0, Qt::AlignHCenter);
    statusCardLayout->addRow(statusField);
    riverLayout->addWidget(statusCard);

    m_accessibilityNotice->setCompact(true);
    riverLayout->addSpacing(settings::groupGap());
    riverLayout->addWidget(m_accessibilityNotice);
    riverLayout->addSpacing(settings::groupGap());

    riverLayout->addWidget(settings::makeSectionLabel(QStringLiteral("Setup at a glance"), river));
    riverLayout->addSpacing(settings::tightSpacing());
    auto *summaryCard = settings::makeSettingsCard(river);
    auto *form = qobject_cast<QFormLayout *>(summaryCard->layout());

    const int valueWidth = fontMetrics().horizontalAdvance(QString(24, QLatin1Char('x')));
    for (QLabel *label : {m_provider, m_microphone, m_output, m_theme}) {
        label->setMaximumWidth(valueWidth);
        label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        label->installEventFilter(this);
    }
    m_provider->setObjectName(QStringLiteral("refinementSummary"));
    m_microphone->setObjectName(QStringLiteral("microphoneSummary"));
    addSummaryRow(form, QStringLiteral("Refinement"),
                  QStringLiteral("Clean up dictated text after capture."), m_provider,
                  AppPageId::Refinement, this, summaryCard);
    addSummaryRow(form, QStringLiteral("Microphone"),
                  QStringLiteral("Input device used for capture."), m_microphone,
                  AppPageId::Audio, this, summaryCard);
    addSummaryRow(form, QStringLiteral("Output"),
                  QStringLiteral("How Speecher delivers final text."), m_output,
                  AppPageId::Output, this, summaryCard);
    addSummaryRow(form, QStringLiteral("Theme"),
                  QStringLiteral("Application color scheme."), m_theme,
                  AppPageId::General, this, summaryCard, false);
    riverLayout->addWidget(summaryCard);
    riverLayout->addStretch();

    pageLayout->addWidget(river, 0, Qt::AlignHCenter | Qt::AlignTop);
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
    updateSummary(false);
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
    m_status->setForegroundRole(QPalette::Highlight);
    m_waveform->setVisible(active);
    if (!active) {
        m_waveform->setLevel(0.0f);
    }
}

void DictationPage::refreshSummary()
{
    updateSummary(true);
}

void DictationPage::updateSummary(bool resolveMicrophone)
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
    if (!deviceId.isEmpty() && !resolveMicrophone) {
        microphone = QStringLiteral("Selected microphone");
    } else if (!deviceId.isEmpty()) {
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
