#include "ui/DictationPage.h"

#include "app/AppFrontEnd.h"
#include "app/PlatformComposition.h"
#include "app/ApplicationController.h"
#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
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
        int y = rect.y();
        QList<QLayoutItem *> row;
        int rowWidth = 0;
        int rowHeight = 0;
        const auto placeRow = [&] {
            // Center each row so the grid doesn't hug the left edge.
            int x = rect.x() + qMax(0, (rect.width() - rowWidth) / 2);
            for (QLayoutItem *item : row) {
                const QSize hint = item->sizeHint();
                if (!testOnly) item->setGeometry(QRect(QPoint(x, y), hint));
                x += hint.width() + m_spacing;
            }
        };
        for (QLayoutItem *item : m_items) {
            const QSize hint = item->sizeHint();
            const int nextWidth = rowWidth + (row.isEmpty() ? 0 : m_spacing) + hint.width();
            if (nextWidth > rect.width() && !row.isEmpty()) {
                placeRow();
                y += rowHeight + m_spacing;
                row.clear();
                rowWidth = 0;
                rowHeight = 0;
            }
            rowWidth += (row.isEmpty() ? 0 : m_spacing) + hint.width();
            row.append(item);
            rowHeight = qMax(rowHeight, hint.height());
        }
        if (!row.isEmpty()) {
            placeRow();
        }
        return y + rowHeight - rect.y();
    }

    QList<QLayoutItem *> m_items;
    int m_spacing = 8;
};

// Freedesktop names first, so themes other than Breeze find them; a theme with
// neither leaves the card text-only rather than showing a blank square.
QIcon themedIcon(const QString &name, const QString &fallback = QString())
{
    return fallback.isEmpty() ? QIcon::fromTheme(name)
                              : QIcon::fromTheme(name, QIcon::fromTheme(fallback));
}

QWidget *makeSummaryCard(const QIcon &cardIcon,
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
    icon->setPixmap(cardIcon.pixmap(16, 16));
    icon->setFixedSize(16, 16);
    icon->setVisible(!cardIcon.isNull());
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
    , m_heroToggle(new QPushButton(this))
    , m_status(new QLabel(this))
    , m_waveform(new WaveformWidget(this))
    , m_transcript(new QPlainTextEdit(this))
    , m_provider(new QLabel(this))
    , m_microphone(new QLabel(this))
    , m_output(new QLabel(this))
    , m_shortcut(new QLabel(this))
{
    auto *pageLayout = new QVBoxLayout(this);
    settings::applyPageMargins(pageLayout);
    pageLayout->setSpacing(settings::relatedSpacing());

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
    // The popup shows a failure for five seconds and cannot take focus, so the
    // reason also stays here until the next session starts.
    m_errorText = new QLabel(hero);
    m_errorText->setObjectName(QStringLiteral("dictationError"));
    m_errorText->setWordWrap(true);
    m_errorText->setAlignment(Qt::AlignHCenter);
    m_errorText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_errorText->hide();
    heroLayout->addWidget(m_errorText);
    heroLayout->addWidget(m_waveform, 0, Qt::AlignHCenter);

    m_transcript->setObjectName(QStringLiteral("dictationTranscript"));
    // A record of what was delivered: edits here would go nowhere, so the
    // text stays read-only and selectable in every state.
    m_transcript->setReadOnly(true);
    m_transcript->setPlaceholderText(
        QStringLiteral("What you say appears here while you dictate."));
    m_transcript->setMinimumHeight(140);
    m_transcript->setMaximumHeight(260);
    heroLayout->addWidget(m_transcript);

    m_copyTranscript = new QToolButton(m_transcript);
    m_copyTranscript->setObjectName(QStringLiteral("copyTranscript"));
    const QIcon copyIcon = themedIcon(QStringLiteral("edit-copy"));
    // Without a themed icon an icon-only button is invisible; fall back to a
    // word.
    m_copyTranscript->setText(QStringLiteral("Copy"));
    m_copyTranscript->setIcon(copyIcon);
    m_copyTranscript->setToolButtonStyle(copyIcon.isNull() ? Qt::ToolButtonTextOnly
                                                           : Qt::ToolButtonIconOnly);
    m_copyTranscript->setAutoRaise(true);
    m_copyTranscript->setCursor(Qt::ArrowCursor);
    m_copyTranscript->setToolTip(QStringLiteral("Copy transcript"));
    m_copyTranscript->setFocusPolicy(Qt::NoFocus);
    m_copyTranscript->hide();
    m_transcript->installEventFilter(this);
    connect(m_copyTranscript, &QToolButton::clicked, this, [this, copyIcon] {
        const QString text = m_transcript->toPlainText();
        if (text.isEmpty()) {
            return;
        }
        QGuiApplication::clipboard()->setText(text);
        m_copyTranscript->setIcon(themedIcon(QStringLiteral("checkmark"),
                                             QStringLiteral("dialog-ok-apply")));
        m_copyTranscript->setText(QStringLiteral("Copied"));
        QTimer::singleShot(1500, m_copyTranscript, [this, copyIcon] {
            m_copyTranscript->setIcon(copyIcon);
            m_copyTranscript->setText(QStringLiteral("Copy"));
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
    for (QLabel *label : {m_provider, m_microphone, m_output, m_shortcut}) {
        label->setFixedWidth(valueWidth);
        label->installEventFilter(this);
    }
    m_provider->setObjectName(QStringLiteral("refinementSummary"));
    m_microphone->setObjectName(QStringLiteral("microphoneSummary"));
    cardsRow->addWidget(makeSummaryCard(themedIcon(QStringLiteral("document-edit"),
                                                   QStringLiteral("tools-wizard")),
                                        QStringLiteral("Refinement"), m_provider,
                                        AppPageId::Refinement, this));
    cardsRow->addWidget(makeSummaryCard(themedIcon(QStringLiteral("audio-input-microphone")),
                                        QStringLiteral("Microphone"), m_microphone,
                                        AppPageId::Audio, this));
    cardsRow->addWidget(makeSummaryCard(themedIcon(QStringLiteral("edit-paste"),
                                                   QStringLiteral("edit-copy")),
                                        QStringLiteral("Output"), m_output,
                                        AppPageId::Output, this));
    // The shortcut editor lives on General, so the card opens that page.
    m_shortcut->setObjectName(QStringLiteral("shortcutSummary"));
    cardsRow->addWidget(makeSummaryCard(themedIcon(QStringLiteral("input-keyboard"),
                                                   QStringLiteral("preferences-desktop-keyboard")),
                                        QStringLiteral("Global Shortcut"), m_shortcut,
                                        AppPageId::General, this));
    columnLayout->addWidget(cardsHost);

    pageLayout->addWidget(column);
    pageLayout->addStretch();

    connect(m_heroToggle, &QPushButton::clicked, controller, &ApplicationController::toggle);
    connect(controller, &ApplicationController::stateChanged, this, &DictationPage::applyState);
    connect(controller, &ApplicationController::statusChanged, this, &DictationPage::setDisplayStatus);
    connect(controller, &ApplicationController::audioLevelChanged, m_waveform, &WaveformWidget::setLevel);
    connect(controller, &ApplicationController::previewChanged, this, [this](const QString &preview) {
        m_transcript->setPlainText(preview);
        m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
    });
    connect(controller, &ApplicationController::transcriptDelivered, this, [this](const QString &text) {
        m_transcript->setPlainText(text);
        m_transcript->verticalScrollBar()->setValue(m_transcript->verticalScrollBar()->maximum());
    });
    connect(controller->session(), &DictationSession::popupErrorRequested, this,
            [this](const QString &message) {
                m_errorText->setText(message.simplified());
                m_errorText->setVisible(!message.simplified().isEmpty());
            });
    connect(controller, &ApplicationController::globalShortcutChanged,
            this, &DictationPage::updateShortcutSummary);
    connect(controller, &ApplicationController::globalShortcutSupportChanged,
            this, &DictationPage::updateShortcutSummary);
    connect(controller, &ApplicationController::globalShortcutRegistrationFinished,
            this, &DictationPage::updateShortcutSummary);
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
    return m_heroToggle;
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
    applyState(status);
    setDisplayStatus(status);
}

void DictationPage::applyState(const QString &stateName)
{
    const QString state = stateName.toCaseFolded();
    const bool active = state == QStringLiteral("starting") || state == QStringLiteral("listening");
    const bool refining = state == QStringLiteral("refining");
    applyToggleState(m_heroToggle, active, refining, state);
    m_waveform->setVisible(active);
    if (active && !m_sessionActive) {
        m_transcript->clear();
        m_errorText->clear();
        m_errorText->hide();
    }
    m_sessionActive = active;
    if (!active) {
        m_waveform->setLevel(0.0f);
    }
}

void DictationPage::setDisplayStatus(const QString &status)
{
    const QString state = status.toCaseFolded();
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
    setSummaryText(m_output, OutputMethod::label(m_controller->settings()->outputMethod()));
    updateShortcutSummary();
}

void DictationPage::updateShortcutSummary()
{
    QString shortcut = m_controller->globalShortcutDisplay();
    if (shortcut.isEmpty()) {
        shortcut = !m_controller->globalShortcutSupportKnown() ? QStringLiteral("Checking…")
            : m_controller->globalShortcutsSupported()         ? QStringLiteral("Not set")
                                                               : QStringLiteral("Set up in your desktop");
    }
    setSummaryText(m_shortcut, shortcut);
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
