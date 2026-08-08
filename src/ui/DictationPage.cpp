#include "ui/DictationPage.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "providers/ProviderRegistry.h"
#include "ui/AccessibilityNotice.h"
#include "ui/WaveformWidget.h"

#include <QFormLayout>
#include <QEvent>
#include <QFontMetrics>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

namespace speecher {

namespace {

QWidget *summaryRow(QLabel *value, const QString &buttonText, AppPageId page,
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
    layout->setSpacing(style()->pixelMetric(QStyle::PM_LayoutVerticalSpacing));
    m_accessibilityNotice->setCompact(true);
    layout->addWidget(m_accessibilityNotice);

    m_toggle->setMinimumHeight(56);
    m_toggle->setMinimumWidth(480);
    m_toggle->setMaximumWidth(480);
    layout->addWidget(m_toggle);
    layout->setAlignment(m_toggle, Qt::AlignHCenter);
    m_status->setAlignment(Qt::AlignHCenter);
    m_status->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_status);
    layout->addWidget(m_waveform, 0, Qt::AlignHCenter);

    auto *summary = new QGroupBox(QStringLiteral("At a glance"), this);
    auto *form = new QFormLayout(summary);
    form->addRow(QStringLiteral("Refinement"),
                 summaryRow(m_provider, QStringLiteral("Change…"), AppPageId::Refinement, this, summary));
    form->addRow(QStringLiteral("Output"),
                 summaryRow(m_output, QStringLiteral("Change…"), AppPageId::Output, this, summary));
    form->addRow(QStringLiteral("Theme"),
                 summaryRow(m_theme, QStringLiteral("Change…"), AppPageId::General, this, summary));
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
    for (QLabel *label : {m_provider, m_output, m_theme}) {
        label->installEventFilter(this);
    }
    setStatus(controller->stateName());
    refreshSummary();
}

void DictationPage::setStatus(const QString &status)
{
    const QString state = status.toCaseFolded();
    const bool active = state == QStringLiteral("starting") || state == QStringLiteral("listening");
    m_toggle->setText(active ? QStringLiteral("Stop") : QStringLiteral("Start"));
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
    const bool muted = state == QStringLiteral("idle");
    m_status->setForegroundRole(muted ? QPalette::PlaceholderText : QPalette::WindowText);
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
    setSummaryText(m_output, m_controller->primaryOutputStatus());
    const QString theme = m_controller->settings()->theme();
    setSummaryText(m_theme, theme.left(1).toUpper() + theme.mid(1));
}

void DictationPage::setCompactShell(bool compact)
{
    m_toggle->setMinimumWidth(compact ? 0 : 480);
    m_toggle->setMaximumWidth(compact ? QWIDGETSIZE_MAX : 480);
    layout()->setAlignment(m_toggle, compact ? Qt::Alignment{} : Qt::AlignHCenter);
}

bool DictationPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize) {
        if (auto *label = qobject_cast<QLabel *>(watched)) {
            const QString fullText = label->property("fullText").toString();
            if (!fullText.isEmpty()) {
                label->setText(label->fontMetrics().elidedText(fullText,
                                                                Qt::ElideRight,
                                                                label->width()));
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DictationPage::setSummaryText(QLabel *label, const QString &text)
{
    label->setProperty("fullText", text);
    label->setToolTip(text);
    label->setText(label->fontMetrics().elidedText(text, Qt::ElideRight, label->width()));
}

} // namespace speecher
