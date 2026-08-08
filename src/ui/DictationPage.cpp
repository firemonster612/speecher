#include "ui/DictationPage.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "providers/ProviderRegistry.h"
#include "ui/AccessibilityNotice.h"
#include "ui/WaveformWidget.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QVBoxLayout>

namespace speecher {

namespace {

void addSummaryRow(QGridLayout *grid,
                   int row,
                   const QString &labelText,
                   QLabel *value,
                   AppPageId page,
                   DictationPage *owner,
                   QWidget *parent)
{
    auto *label = new QLabel(labelText, parent);
    auto *change = new QPushButton(QStringLiteral("Change…"), parent);
    value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    grid->addWidget(label, row, 0, Qt::AlignLeft | Qt::AlignVCenter);
    grid->addWidget(value, row, 1, Qt::AlignVCenter);
    grid->addWidget(change, row, 2, Qt::AlignLeft | Qt::AlignVCenter);
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
    , m_output(new QLabel(this))
    , m_theme(new QLabel(this))
{
    auto *pageLayout = new QVBoxLayout(this);
    settings::applyPageMargins(pageLayout);
    auto *column = new QWidget(this);
    column->setObjectName(QStringLiteral("dictationContentColumn"));
    column->setMaximumWidth(560);
    column->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(settings::makePageTitle(QStringLiteral("Dictation"), column));
    layout->addSpacing(settings::sectionGap());

    auto *groups = new QWidget(column);
    auto *groupsLayout = new QVBoxLayout(groups);
    groupsLayout->setContentsMargins(0, 0, 0, 0);
    groupsLayout->setSpacing(settings::groupGap());
    m_accessibilityNotice->setCompact(true);
    groupsLayout->addWidget(m_accessibilityNotice);

    auto *dictationControls = new QWidget(groups);
    auto *dictationLayout = new QVBoxLayout(dictationControls);
    dictationLayout->setContentsMargins(0, 0, 0, 0);
    dictationLayout->setSpacing(settings::relatedSpacing());
    m_toggle->setMinimumWidth(0);
    m_toggle->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    dictationLayout->addWidget(m_toggle, 0, Qt::AlignHCenter);
    m_status->setAlignment(Qt::AlignHCenter);
    m_status->setForegroundRole(QPalette::WindowText);
    dictationLayout->addWidget(m_status);
    dictationLayout->addWidget(m_waveform, 0, Qt::AlignHCenter);
    groupsLayout->addWidget(dictationControls);

    auto *summaryGroup = new QWidget(groups);
    auto *summaryLayout = new QVBoxLayout(summaryGroup);
    summaryLayout->setContentsMargins(0, 0, 0, 0);
    summaryLayout->setSpacing(settings::tightSpacing());
    summaryLayout->addWidget(settings::makeSectionLabel(QStringLiteral("At a glance"), summaryGroup));
    auto *summary = new QWidget(summaryGroup);
    auto *grid = new QGridLayout(summary);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setColumnStretch(1, 1);
    addSummaryRow(grid, 0, QStringLiteral("Refinement"), m_provider,
                  AppPageId::Refinement, this, summary);
    addSummaryRow(grid, 1, QStringLiteral("Output"), m_output,
                  AppPageId::Output, this, summary);
    addSummaryRow(grid, 2, QStringLiteral("Theme"), m_theme,
                  AppPageId::General, this, summary);
    summaryLayout->addWidget(summary);
    groupsLayout->addWidget(summaryGroup);
    layout->addWidget(groups);
    layout->addStretch();
    auto *contentRow = new QHBoxLayout;
    contentRow->setContentsMargins(0, 0, 0, 0);
    contentRow->addStretch();
    contentRow->addWidget(column, 1);
    contentRow->addStretch();
    pageLayout->addLayout(contentRow);
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
