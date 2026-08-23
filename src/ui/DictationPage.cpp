#include "ui/DictationPage.h"

#include "app/LinuxComposition.h"
#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "providers/ProviderRegistry.h"
#include "ui/AccessibilityNotice.h"
#include "ui/WaveformWidget.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QClipboard>
#include <QEvent>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QGuiApplication>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace speecher {

namespace {


// Minimal flow layout (Qt's FlowLayout example): wraps items to new rows
// when the width runs out instead of squeezing them into overlap.
class FlowLayout final : public QLayout {
public:
    explicit FlowLayout(int spacing, QWidget *parent = nullptr)
        : QLayout(parent), m_spacing(spacing) {}
    ~FlowLayout() override { while (QLayoutItem *item = takeAt(0)) delete item; }
    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return m_items.size(); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override
    { return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
    QSize minimumSize() const override
    {
        QSize size;
        for (QLayoutItem *item : m_items) size = size.expandedTo(item->minimumSize());
        return size;
    }
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        doLayout(rect, false);
    }

private:
    int doLayout(const QRect &rect, bool testOnly) const
    {
        int x = rect.x();
        int y = rect.y();
        int rowHeight = 0;
        for (QLayoutItem *item : m_items) {
            const QSize hint = item->sizeHint();
            if (x + hint.width() > rect.right() + 1 && rowHeight > 0) {
                x = rect.x();
                y += rowHeight + m_spacing;
                rowHeight = 0;
            }
            if (!testOnly) item->setGeometry(QRect(QPoint(x, y), hint));
            x += hint.width() + m_spacing;
            rowHeight = qMax(rowHeight, hint.height());
        }
        return y + rowHeight - rect.y();
    }

    QList<QLayoutItem *> m_items;
    int m_spacing = 8;
};

QWidget *makeSummaryCard(const QString &iconName,
                         const QString &title,
                         QLabel *value,
                         AppPageId page,
                         DictationPage *owner)
{
    // A real push button so the card carries native hover/press/focus states;
    // the content is laid out on top of the button face.
    auto *card = new QPushButton(owner);
    card->setObjectName(QStringLiteral("summaryCard"));
    card->setCursor(Qt::PointingHandCursor);
    card->setProperty("navTarget", static_cast<int>(page));
    card->setToolTip(QStringLiteral("Open %1 settings").arg(title));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(2);

    auto *titleRow = new QWidget(card);
    auto *titleLayout = new QHBoxLayout(titleRow);
    titleLayout->setContentsMargins(0, 0, 0, 0);
    titleLayout->setSpacing(6);
    auto *icon = new QLabel(titleRow);
    icon->setPixmap(QIcon::fromTheme(iconName).pixmap(16, 16));
    icon->setFixedSize(16, 16);
    auto *titleLabel = new QLabel(title, titleRow);
    titleLayout->addWidget(icon, 0, Qt::AlignVCenter);
    titleLayout->addWidget(titleLabel, 0, Qt::AlignVCenter);
    titleLayout->addStretch();

    value->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(titleRow);
    layout->addWidget(value);
    const QSize content = layout->sizeHint().grownBy(QMargins(4, 2, 4, 2));
    card->setMinimumSize(content);
    card->setFixedSize(content.expandedTo(QSize(220, 0)));

    for (QWidget *child : {static_cast<QWidget *>(titleRow), static_cast<QWidget *>(value)}) {
        child->setAttribute(Qt::WA_TransparentForMouseEvents);
    }
    icon->setAttribute(Qt::WA_TransparentForMouseEvents);
    QObject::connect(card, &QPushButton::clicked, owner,
                     [owner, page] { emit owner->navigateRequested(page); });
    return card;
}

} // namespace

DictationPage::DictationPage(ApplicationController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_accessibilityNotice(new AccessibilityNotice(this))
    , m_toggle(new QPushButton(this))
    , m_heroToggle(new QPushButton(this))
    , m_status(new QLabel(this))
    , m_waveform(new WaveformWidget(this))
    , m_transcript(new QPlainTextEdit(this))
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

    auto *column = new QWidget(this);
    auto *columnLayout = new QVBoxLayout(column);
    columnLayout->setContentsMargins(0, 0, 0, 0);
    columnLayout->setSpacing(settings::relatedSpacing());

    m_accessibilityNotice->setCompact(true);
    columnLayout->addWidget(m_accessibilityNotice);

    auto *hero = new QFrame(column);
    hero->setObjectName(QStringLiteral("settingsCard"));
    hero->setFrameShape(QFrame::StyledPanel);
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(16, 14, 16, 14);
    heroLayout->setSpacing(settings::tightSpacing());

    QFont statusFont = m_status->font();
    statusFont.setPointSize(statusFont.pointSize() + 1);
    statusFont.setBold(true);
    m_status->setFont(statusFont);
    m_status->setForegroundRole(QPalette::WindowText);
    heroLayout->addWidget(m_status, 0, Qt::AlignHCenter);
    heroLayout->addWidget(m_waveform, 0, Qt::AlignHCenter);

    m_transcript->setObjectName(QStringLiteral("dictationTranscript"));
    m_transcript->setReadOnly(true);
    m_transcript->setPlaceholderText(
        QStringLiteral("What you say appears here while you dictate."));
    m_transcript->setMinimumHeight(140);
    m_transcript->setMaximumHeight(260);
    heroLayout->addWidget(m_transcript);

    m_copyTranscript = new QToolButton(m_transcript);
    m_copyTranscript->setObjectName(QStringLiteral("copyTranscript"));
    m_copyTranscript->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
    m_copyTranscript->setAutoRaise(true);
    m_copyTranscript->setCursor(Qt::ArrowCursor);
    m_copyTranscript->setToolTip(QStringLiteral("Copy transcript"));
    m_copyTranscript->setFocusPolicy(Qt::NoFocus);
    m_copyTranscript->hide();
    m_transcript->installEventFilter(this);
    connect(m_copyTranscript, &QToolButton::clicked, this, [this] {
        const QString text = m_transcript->toPlainText();
        if (text.isEmpty()) {
            return;
        }
        QGuiApplication::clipboard()->setText(text);
        m_copyTranscript->setIcon(QIcon::fromTheme(QStringLiteral("checkmark"),
                                                   QIcon::fromTheme(QStringLiteral("dialog-ok-apply"))));
        QTimer::singleShot(1500, m_copyTranscript, [this] {
            m_copyTranscript->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
        });
    });

    m_heroToggle->setObjectName(QStringLiteral("dictationHeroToggle"));
    m_heroToggle->setMinimumHeight(36);
    m_heroToggle->setMinimumWidth(200);
    heroLayout->addSpacing(settings::tightSpacing());
    heroLayout->addWidget(m_heroToggle, 0, Qt::AlignHCenter);
    columnLayout->addWidget(hero);

    auto *cardsHost = new QWidget(column);
    auto *cardsRow = new FlowLayout(settings::relatedSpacing(), cardsHost);
    cardsRow->setContentsMargins(0, 0, 0, 0);
    const int valueWidth = fontMetrics().horizontalAdvance(QString(22, QLatin1Char('x')));
    for (QLabel *label : {m_provider, m_microphone, m_output, m_theme}) {
        label->setFixedWidth(valueWidth);
        label->installEventFilter(this);
    }
    m_provider->setObjectName(QStringLiteral("refinementSummary"));
    m_microphone->setObjectName(QStringLiteral("microphoneSummary"));
    cardsRow->addWidget(makeSummaryCard(QStringLiteral("tools-wizard"),
                                        QStringLiteral("Refinement"), m_provider,
                                        AppPageId::Refinement, this));
    cardsRow->addWidget(makeSummaryCard(QStringLiteral("audio-input-microphone"),
                                        QStringLiteral("Microphone"), m_microphone,
                                        AppPageId::Audio, this));
    cardsRow->addWidget(makeSummaryCard(QStringLiteral("klipper"),
                                        QStringLiteral("Output"), m_output,
                                        AppPageId::Output, this));
    cardsRow->addWidget(makeSummaryCard(QStringLiteral("preferences-system"),
                                        QStringLiteral("Theme"), m_theme,
                                        AppPageId::General, this));
    columnLayout->addWidget(cardsHost);

    pageLayout->addWidget(column);
    pageLayout->addStretch();

    connect(m_toggle, &QPushButton::clicked, controller, &ApplicationController::toggle);
    connect(m_heroToggle, &QPushButton::clicked, controller, &ApplicationController::toggle);
    connect(controller, &ApplicationController::statusChanged, this, &DictationPage::setStatus);
    connect(controller, &ApplicationController::audioLevelChanged, m_waveform, &WaveformWidget::setLevel);
    connect(controller, &ApplicationController::previewChanged, this, [this](const QString &preview) {
        m_transcript->setPlainText(preview);
        m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
    });
    connect(controller, &ApplicationController::transcriptDelivered, this, [this](const QString &text) {
        m_transcript->setPlainText(text);
        m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
    });
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

void DictationPage::applyToggleState(QPushButton *button,
                                     bool active,
                                     bool refining,
                                     const QString &state) const
{
    const bool busy = state == QStringLiteral("stopping") || state == QStringLiteral("delivering");
    button->setEnabled(!busy);
    button->setText(active
                        ? QStringLiteral("Stop Dictation")
                        : refining
                            ? QStringLiteral("Cancel Refinement")
                            : state == QStringLiteral("stopping")
                                ? QStringLiteral("Stopping…")
                                : state == QStringLiteral("delivering")
                                    ? QStringLiteral("Delivering…")
                                    : QStringLiteral("Start Dictation"));
    button->setIcon(QIcon::fromTheme(active || refining
                                         ? QStringLiteral("media-playback-stop")
                                         : QStringLiteral("media-record")));
}

void DictationPage::setStatus(const QString &status)
{
    const QString state = status.toCaseFolded();
    const bool active = state == QStringLiteral("starting") || state == QStringLiteral("listening");
    const bool refining = state == QStringLiteral("refining");
    applyToggleState(m_toggle, active, refining, state);
    applyToggleState(m_heroToggle, active, refining, state);
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
    if (active && !m_sessionActive) {
        m_transcript->clear();
    }
    m_sessionActive = active;
    if (!active) {
        m_waveform->setLevel(0.0f);
    }
    // The transcript is live output while a session runs; once the session is
    // over the delivered text stays and can be edited, selected, and copied.
    const bool sessionRunning = active || refining
        || state == QStringLiteral("stopping") || state == QStringLiteral("delivering");
    m_transcript->setReadOnly(sessionRunning);
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
                                                                label->width()));
            }
        }
    }
    if (watched == m_transcript) {
        switch (event->type()) {
        case QEvent::Resize:
            m_copyTranscript->move(m_transcript->width()
                                       - m_copyTranscript->sizeHint().width() - 6,
                                   6);
            break;
        case QEvent::Enter:
            m_copyTranscript->setVisible(!m_transcript->toPlainText().isEmpty());
            break;
        case QEvent::Leave:
            if (!m_copyTranscript->underMouse()) {
                m_copyTranscript->hide();
            }
            break;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void DictationPage::setSummaryText(QLabel *label, const QString &text)
{
    label->setProperty("fullText", text);
    label->setToolTip(text);
    label->setText(label->fontMetrics().elidedText(
        text, Qt::ElideRight, label->width() > 0 ? label->width() : label->minimumWidth()));
}

} // namespace speecher
