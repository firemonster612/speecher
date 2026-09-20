#include "ui/setup/SetupPages.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationPorts.h"
#ifdef SPEECHER_WITH_YDOTOOL
#include "output/YdotoolSetup.h"
#include "output/YdotoolSetupFlow.h"
#endif
#include "providers/ProviderRegistry.h"
#include "ui/settings/SettingsPageSupport.h"
#ifdef Q_OS_LINUX
#include "ui/setup/LinuxGlobalShortcutSetupPage.h"
#endif

#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <memory>

namespace speecher {

int setupPageMargin()
{
    return 24;
}

QPixmap providerMark(const QString &providerId, int size, qreal devicePixelRatio)
{
    // The transcription and refinement pages name the same two companies under
    // different ids, and both rows carry the company's mark.
    static const QHash<QString, QString> marks{
        {QStringLiteral("codex"), QStringLiteral(":/brand/chatgpt.svg")},
        {QStringLiteral("openai"), QStringLiteral(":/brand/chatgpt.svg")},
        {QStringLiteral("claude"), QStringLiteral(":/brand/claude.svg")},
        {QStringLiteral("anthropic"), QStringLiteral(":/brand/claude.svg")},
    };
    const QString resource = marks.value(providerId);
    if (resource.isEmpty()) {
        return {};
    }
    // The marks keep their own colours, which is what makes them recognisable
    // at this size; a palette-coloured silhouette of either one does not read.
    // QIcon renders the SVG at the ratio asked for, so the mark stays sharp on
    // a scaled display.
    return QIcon(resource).pixmap(QSize(size, size), devicePixelRatio);
}

void setCardRowVisible(QWidget *row, bool visible)
{
    row->setVisible(visible);
    QWidget *host = row->parentWidget();
    auto *form = host ? qobject_cast<QFormLayout *>(host->layout()) : nullptr;
    if (!form) {
        return;
    }
    for (int index = 1; index < form->rowCount(); ++index) {
        QLayoutItem *item = form->itemAt(index, QFormLayout::SpanningRole);
        if (!item || item->widget() != row) {
            continue;
        }
        QLayoutItem *above = form->itemAt(index - 1, QFormLayout::SpanningRole);
        if (above && above->widget()
            && above->widget()->objectName() == QLatin1String("rowSeparator")) {
            above->widget()->setVisible(visible);
        }
        return;
    }
}

void setSetupStepCounter(QWidget *page, int step, int total)
{
    auto *layout = qobject_cast<QVBoxLayout *>(page->layout());
    if (!layout) {
        return;
    }
    auto *counter = new QLabel(QStringLiteral("Step %1 of %2").arg(step).arg(total), page);
    counter->setObjectName(QStringLiteral("setupStepCounter"));
    counter->setFont(settings::smallFont(counter->font()));
    counter->setForegroundRole(QPalette::PlaceholderText);
    layout->insertWidget(0, counter, 0, Qt::AlignRight);
}

ProviderStatsBlock::ProviderStatsBlock(QWidget *parent)
    : QWidget(parent)
    , m_rows(new QFormLayout(this))
{
    m_rows->setContentsMargins(0, 0, 0, 0);
    m_rows->setHorizontalSpacing(settings::largeSpacing());
    m_rows->setVerticalSpacing(settings::smallSpacing());
    m_rows->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
}

void ProviderStatsBlock::setStats(const QVector<ProviderStat> &stats)
{
    while (m_rows->rowCount() > 0) {
        m_rows->removeRow(0);
    }
    for (const ProviderStat &stat : stats) {
        auto *name = new QLabel(stat.label, this);
        name->setFont(settings::smallFont(name->font()));
        name->setForegroundRole(QPalette::PlaceholderText);
        auto *value = new QLabel(stat.value, this);
        value->setFont(settings::smallFont(value->font()));
        value->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        m_rows->addRow(name, value);
    }
    setVisible(m_rows->rowCount() > 0);
}

namespace {

// A word-wrapped QLabel that needs more lines than its width-blind size hint
// paints its last line clipped; keeping minimumHeight at heightForWidth makes
// the layout give it the real height (same fix as the settings rows').
class WrappingLabel final : public QLabel {
public:
    using QLabel::QLabel;

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        setMinimumHeight(0);
        setMinimumHeight(heightForWidth(event->size().width()));
    }
};

QVBoxLayout *makePage(QWidget *page, const QString &description, QLabel **introOut = nullptr)
{
    auto *layout = new QVBoxLayout(page);
    const int margin = setupPageMargin();
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(settings::largeSpacing());

    auto *intro = new WrappingLabel(description, page);
    intro->setObjectName(QStringLiteral("setupPageIntro"));
    intro->setWordWrap(true);
    layout->addWidget(intro);
    if (introOut) {
        *introOut = intro;
    }
    return layout;
}

// The provider marks read at the height of one line of text, so they stay in
// proportion to the name beside them at any font size.
int markSize()
{
    return settings::gridUnit();
}

// The provider's own mark, or nothing for a provider that has none.
QLabel *makeProviderMark(const QString &providerId, QWidget *parent)
{
    const QPixmap mark = providerMark(providerId, markSize(), parent->devicePixelRatioF());
    if (mark.isNull()) {
        return nullptr;
    }
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("providerMark_") + providerId);
    label->setPixmap(mark);
    label->setFixedSize(markSize(), markSize());
    return label;
}

// A themed glyph at mark size, for the rows that carry a verdict rather than a
// brand. The style's own standard icon is the fallback, so the row is never
// left with a hole where an icon theme is missing.
QLabel *makeGlyph(QWidget *parent, const QString &themeName, QStyle::StandardPixmap fallback)
{
    const QIcon icon = QIcon::fromTheme(themeName, parent->style()->standardIcon(fallback));
    auto *label = new QLabel(parent);
    label->setPixmap(icon.pixmap(markSize(), markSize()));
    label->setFixedSize(markSize(), markSize());
    return label;
}

// One card row shaped like the mockup's: an optional mark, a name that wraps,
// a right-aligned status, and a small grey line under the name for a hint or a
// reason. The trailing widget, where one is given, sits where the status would.
struct StatusRow {
    QWidget *widget = nullptr;
    QLabel *name = nullptr;
    QLabel *status = nullptr;
    QLabel *hint = nullptr;
};

StatusRow makeStatusRow(QWidget *parent,
                        QWidget *mark,
                        const QString &name,
                        bool boldName,
                        QWidget *trailing = nullptr)
{
    StatusRow row;
    row.widget = new QWidget(parent);
    auto *layout = new QVBoxLayout(row.widget);
    layout->setContentsMargins(settings::rowPadding());
    layout->setSpacing(settings::smallSpacing());

    auto *top = new QHBoxLayout;
    top->setSpacing(settings::largeSpacing());
    int indent = 0;
    if (mark) {
        mark->setParent(row.widget);
        top->addWidget(mark, 0, Qt::AlignVCenter);
        indent = markSize() + settings::largeSpacing();
    }
    row.name = new WrappingLabel(name, row.widget);
    row.name->setWordWrap(true);
    if (boldName) {
        QFont font = row.name->font();
        font.setBold(true);
        row.name->setFont(font);
    }
    top->addWidget(row.name, 1, Qt::AlignVCenter);
    row.status = new QLabel(row.widget);
    top->addWidget(row.status, 0, Qt::AlignRight | Qt::AlignVCenter);
    if (trailing) {
        trailing->setParent(row.widget);
        top->addWidget(trailing, 0, Qt::AlignRight | Qt::AlignVCenter);
    }
    layout->addLayout(top);

    // Reads under the name rather than under the mark, as the mockup's hint
    // lines do.
    row.hint = new WrappingLabel(row.widget);
    row.hint->setWordWrap(true);
    row.hint->setFont(settings::smallFont(row.hint->font()));
    row.hint->setForegroundRole(QPalette::PlaceholderText);
    row.hint->setContentsMargins(indent, 0, 0, 0);
    row.hint->hide();
    layout->addWidget(row.hint);
    return row;
}

// A card built into a container that is already on screen has to be shown
// explicitly: a widget only inherits its parent's visibility at the moment the
// parent is shown, and these lists are rebuilt long after that.
void showRebuiltList(QWidget *list)
{
    for (QWidget *child : list->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly)) {
        child->show();
    }
}

// A titled card: the bold header above, the FormCard container below. Returns
// the card's row layout, which addCardRow() fills.
QFormLayout *addCard(QVBoxLayout *layout, QWidget *parent, const QString &title)
{
    // Header and card travel together, tight against each other, whatever the
    // page's own spacing is.
    auto *section = new QWidget(parent);
    auto *sectionLayout = new QVBoxLayout(section);
    sectionLayout->setContentsMargins(0, 0, 0, 0);
    sectionLayout->setSpacing(0);
    if (!title.isEmpty()) {
        sectionLayout->addWidget(settings::makeSectionLabel(title, section));
    }
    QFrame *card = settings::makeSettingsCard(section);
    sectionLayout->addWidget(card);
    layout->addWidget(section);
    return settings::cardFormLayout(card);
}

void setStatusColor(QLabel *label, bool positive)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText,
                     positive ? settings::positiveTextColor(palette)
                              : label->parentWidget()->palette().color(QPalette::WindowText));
    label->setPalette(palette);
}

void addProfiles(QComboBox *combo)
{
    combo->addItem(QStringLiteral("Work"), QStringLiteral("work"));
    combo->addItem(QStringLiteral("Email"), QStringLiteral("email"));
    combo->addItem(QStringLiteral("Personal"), QStringLiteral("personal"));
    combo->addItem(QStringLiteral("Other"), QStringLiteral("other"));
    combo->addItem(QStringLiteral("AI coding"), QStringLiteral("ai_coding"));
}

void addCleanupLevels(QComboBox *combo)
{
    combo->addItem(QStringLiteral("None"), QStringLiteral("none"));
    combo->addItem(QStringLiteral("Light"), QStringLiteral("light_cleanup"));
    combo->addItem(QStringLiteral("Medium"), QStringLiteral("balanced"));
    combo->addItem(QStringLiteral("High"), QStringLiteral("strong_polish"));
}

void addTones(QComboBox *combo)
{
    combo->addItem(QStringLiteral("No tone override"), QStringLiteral("none"));
    combo->addItem(QStringLiteral("Formal"), QStringLiteral("formal"));
    combo->addItem(QStringLiteral("Casual"), QStringLiteral("casual"));
    combo->addItem(QStringLiteral("Very casual"), QStringLiteral("very_casual"));
    combo->addItem(QStringLiteral("Excited"), QStringLiteral("excited"));
    combo->addItem(QStringLiteral("Gen Z"), QStringLiteral("gen_z"));
}

// Runs work on a throwaway thread and delivers its result on context's thread.
// Whether a late result still matters is the caller's business.
template <typename Result>
void runOffThread(QObject *context,
                  std::function<Result()> work,
                  std::function<void(const Result &)> done)
{
    auto result = std::make_shared<Result>();
    QThread *thread = QThread::create([work, result] { *result = work(); });
    QObject::connect(thread, &QThread::finished, context, [result, done] { done(*result); });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

// One selectable provider as a card row: the company's mark, the name in bold
// on the radio, and what the probe found on the right. An optional note reads
// under the name, indented past the mark. Both pages' option lists use this.
ProviderOptionRow addOptionRow(QFormLayout *card,
                               QButtonGroup *group,
                               const QString &id,
                               const QString &label,
                               const QString &note,
                               const QString &objectNamePrefix)
{
    QWidget *host = card->parentWidget();
    auto *row = new QWidget(host);
    auto *layout = new QVBoxLayout(row);
    layout->setContentsMargins(settings::rowPadding());
    layout->setSpacing(settings::smallSpacing());

    auto *top = new QHBoxLayout;
    top->setSpacing(settings::largeSpacing());
    int indent = 0;
    if (QLabel *mark = makeProviderMark(id, row)) {
        top->addWidget(mark, 0, Qt::AlignVCenter);
        indent = markSize() + settings::largeSpacing();
    }
    auto *button = new QRadioButton(label, row);
    button->setObjectName(objectNamePrefix + QStringLiteral("Option_") + id);
    QFont font = button->font();
    font.setBold(true);
    button->setFont(font);
    top->addWidget(button, 1, Qt::AlignVCenter);
    auto *status = new QLabel(row);
    status->setObjectName(objectNamePrefix + QStringLiteral("Status_") + id);
    top->addWidget(status, 0, Qt::AlignRight | Qt::AlignVCenter);
    layout->addLayout(top);

    if (!note.isEmpty()) {
        auto *noteLabel = new WrappingLabel(note, row);
        noteLabel->setWordWrap(true);
        noteLabel->setFont(settings::smallFont(noteLabel->font()));
        noteLabel->setForegroundRole(QPalette::PlaceholderText);
        noteLabel->setContentsMargins(indent, 0, 0, 0);
        layout->addWidget(noteLabel);
    }
    settings::addCardRow(card, row, host);
    group->addButton(button);
    return {id, label, button, status};
}

// The short line naming which sign-in a refinement provider uses. The
// registry's setup hint explains how to install the CLI, which is more than
// this row needs; a provider without a line of its own falls back to it.
QString credentialNote(const ProviderDescriptor &provider)
{
    static const QHash<QString, QString> notes{
        {QStringLiteral("anthropic"), QStringLiteral("Uses your Claude Code sign-in.")},
        {QStringLiteral("openai"), QStringLiteral("Uses your ChatGPT or Codex sign-in.")},
    };
    return notes.value(provider.id, provider.setupHint);
}

// What the shortcut actually does depends on the activation mode chosen a
// page earlier, so the closing instruction has to follow it.
QString activationInstruction(ShortcutActivationMode mode, const QString &shortcut)
{
    switch (mode) {
    case ShortcutActivationMode::Toggle:
        return QStringLiteral("press %1 to start, press it again to stop").arg(shortcut);
    case ShortcutActivationMode::PushToTalk:
        return QStringLiteral("hold %1 while you speak").arg(shortcut);
    case ShortcutActivationMode::Hybrid:
        break;
    }
    return QStringLiteral("tap %1 to toggle, or hold it to dictate until release").arg(shortcut);
}

QString profileLabel(WritingProfile profile)
{
    switch (profile) {
    case WritingProfile::Work:
        return QStringLiteral("Work");
    case WritingProfile::Email:
        return QStringLiteral("Email");
    case WritingProfile::Personal:
        return QStringLiteral("Personal");
    case WritingProfile::Other:
        return QStringLiteral("Other");
    case WritingProfile::AiCoding:
        return QStringLiteral("AI coding");
    }
    return QStringLiteral("Other");
}

} // namespace

WelcomeSetupPage::WelcomeSetupPage(SettingsStore &settings,
                                   ProviderRegistry &providers,
                                   QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_providers(providers)
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Speecher records a short dictation, turns it into text, and sends it to the app you were using."));
    QString checksText = QStringLiteral(
        "This assistant checks everything dictation needs: your speech service, microphone, and how text reaches your apps.");
#ifdef Q_OS_LINUX
    checksText += QStringLiteral(" It ends by setting up a Global Shortcut.");
#endif
    auto *checks = new QLabel(checksText, this);
    checks->setWordWrap(true);
    layout->addWidget(checks);

    // Nothing later in the assistant can succeed without one of these
    // sign-ins, so the one real prerequisite is stated on the first page.
    QFormLayout *card = addCard(layout, this, QStringLiteral("Before you start"));
    QWidget *host = card->parentWidget();

    auto *leadRow = new QWidget(host);
    auto *leadLayout = new QVBoxLayout(leadRow);
    leadLayout->setContentsMargins(settings::rowPadding());
    auto *lead = new WrappingLabel(
        QStringLiteral("Speecher uses your existing ChatGPT or Claude sign-in. Install and sign in to one of these, then choose Check again:"),
        leadRow);
    lead->setWordWrap(true);
    leadLayout->addWidget(lead);
    settings::addCardRow(card, leadRow, host);

    for (const ProviderDescriptor &provider : m_providers.speechProviders()) {
        const StatusRow row = makeStatusRow(host,
                                            makeProviderMark(provider.id, host),
                                            credentialSourceLabel(provider.id, provider.label),
                                            false);
        row.status->setObjectName(QStringLiteral("welcomeCredentialStatus_") + provider.id);
        row.status->setText(QStringLiteral("Checking…"));
        row.hint->setObjectName(QStringLiteral("welcomeCredentialHint_") + provider.id);
        row.hint->setText(provider.setupHint);
        settings::addCardRow(card, row.widget, host);
        m_rows.append({provider.id, row.status, row.hint, false});
    }

    auto *checkAgain = new QPushButton(QStringLiteral("Check again"), this);
    checkAgain->setObjectName(QStringLiteral("welcomeCheckAgain"));
    checkAgain->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(checkAgain, &QPushButton::clicked, this, &WelcomeSetupPage::checkCredentials);
    layout->addWidget(checkAgain, 0, Qt::AlignLeft);

    layout->addStretch();
    // The first showEvent runs the first probe. Probing from here as well
    // aimed two rounds at the same providers before the page was even visible.
}

void WelcomeSetupPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // A sign-in performed in a terminal while the assistant sat open counts
    // as soon as the user comes back to this page.
    checkCredentials();
}

void WelcomeSetupPage::recheck()
{
    checkCredentials();
}

void WelcomeSetupPage::checkCredentials()
{
    const quint64 generation = ++m_checkGeneration;
    m_checksOutstanding = 0;
    const SpeechSettings speech = m_settings.snapshot().speech;
    for (int index = 0; index < m_rows.size(); ++index) {
        const CredentialRow &row = m_rows.at(index);
        SpeechTranscriber *provider = m_providers.speechProvider(row.providerId);
        if (!provider) {
            showCredential(index, false);
            continue;
        }
        setStatusColor(row.status, false);
        row.status->setText(QStringLiteral("Checking…"));
        row.hint->hide();

        std::optional<SpeechPrepareJob> job = provider->createPrepareJob(speech);
        if (!job || !job->run) {
            showCredential(index, provider->prepare(speech).ok);
            continue;
        }
        auto prepareJob = std::make_shared<SpeechPrepareJob>(std::move(*job));
        ++m_checksOutstanding;
        runOffThread<SpeechPrepareResult>(
            this,
            [prepareJob] { return prepareJob->run(); },
            [this, index, generation, prepareJob](const SpeechPrepareResult &result) {
                // A newer round owns the counter now; this answer is stale.
                if (generation != m_checkGeneration) {
                    return;
                }
                if (prepareJob->apply) {
                    prepareJob->apply(result);
                }
                showCredential(index, result.ok);
                --m_checksOutstanding;
                if (m_checksOutstanding == 0) {
                    emit checkFinished();
                }
            });
    }
    if (m_checksOutstanding == 0) {
        emit checkFinished();
    }
}

void WelcomeSetupPage::showCredential(int index, bool found)
{
    CredentialRow &row = m_rows[index];
    row.found = found;
    setStatusColor(row.status, found);
    row.status->setText(found ? QStringLiteral("Sign-in found")
                              : QStringLiteral("Not found"));
    row.hint->setVisible(!found && !row.hint->text().isEmpty());
    const bool anyFound = std::any_of(m_rows.cbegin(), m_rows.cend(),
                                      [](const CredentialRow &row) { return row.found; });
    // With no speech providers registered at all there is nothing to sign in
    // to, and holding Next would strand the user on page one.
    setReady(m_rows.isEmpty() || anyFound);
}

QString WelcomeSetupPage::blockedReason() const
{
    return QStringLiteral("No ChatGPT or Claude sign-in was found.");
}

void WelcomeSetupPage::setReady(bool ready)
{
    if (m_ready == ready) {
        return;
    }
    m_ready = ready;
    emit readyChanged();
}

SpeechProviderSetupPage::SpeechProviderSetupPage(SettingsStore &settings,
                                                 ProviderRegistry &providers,
                                                 QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_accuracyPass(new QCheckBox(QStringLiteral("Extra transcription accuracy (will increase transcription time)"), this))
    , m_stats(new ProviderStatsBlock(this))
    , m_hint(new WrappingLabel(this))
    , m_status(new WrappingLabel(this))
    , m_checkAgain(new QPushButton(QStringLiteral("Check again"), this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Choose the service Speecher uses to turn speech into a raw transcript."));

    // Every service is on the page with its own readiness, so the choice does
    // not hide behind a dropdown the user has to open to find it.
    QFormLayout *choices = addCard(layout, this, QStringLiteral("Transcription service"));
    auto *group = new QButtonGroup(this);
    const QString savedProvider = m_settings.speechProvider();
    for (const ProviderDescriptor &provider : m_providers.speechProviders()) {
        m_options.append(addOptionRow(choices, group, provider.id, provider.label,
                                      QString(), QStringLiteral("speechProvider")));
        m_options.last().button->setChecked(provider.id == savedProvider);
    }
    if (!m_options.isEmpty() && selectedIndex() < 0) {
        m_options.first().button->setChecked(true);
    }

    m_hint->setObjectName(QStringLiteral("speechProviderHint"));
    m_hint->setWordWrap(true);
    m_status->setObjectName(QStringLiteral("speechProviderStatus"));
    m_status->setWordWrap(true);
    m_checkAgain->setObjectName(QStringLiteral("speechProviderCheckAgain"));
    m_checkAgain->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_accuracyPass->setObjectName(QStringLiteral("codexFinalRetranscribe"));
    m_accuracyPass->setChecked(m_settings.codexFinalRetranscribe());
    layout->addWidget(m_stats);
    // Under the facts about the chosen service, where the refinement page puts
    // Fast mode: it is a setting for that service, not part of the sign-in
    // verdict below it. Codex only; see selectProvider().
    layout->addWidget(m_accuracyPass);
    layout->addWidget(m_status);
    layout->addWidget(m_hint);
    layout->addWidget(m_checkAgain, 0, Qt::AlignLeft);
    layout->addStretch();

    for (const ProviderOptionRow &option : m_options) {
        const QString providerId = option.id;
        connect(option.button, &QRadioButton::clicked, this, [this] { m_userSelected = true; });
        connect(option.button, &QRadioButton::toggled, this, [this, providerId](bool checked) {
            if (checked) {
                selectProvider(providerId);
            }
        });
    }
    connect(m_checkAgain, &QPushButton::clicked,
            this, &SpeechProviderSetupPage::checkProviders);
    connect(m_accuracyPass, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.setCodexFinalRetranscribe(checked);
    });
    selectProvider(m_options.isEmpty() ? QString()
                                       : m_options.at(std::max(0, selectedIndex())).id);
}

void SpeechProviderSetupPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Probing here rather than in the constructor keeps this page's credential
    // reads off the Welcome page's, which contend for the same lock file, and
    // catches a sign-in the user performed after passing Welcome.
    checkProviders();
}

void SpeechProviderSetupPage::recheck()
{
    checkProviders();
}

int SpeechProviderSetupPage::selectedIndex() const
{
    for (int index = 0; index < m_options.size(); ++index) {
        if (m_options.at(index).button->isChecked()) {
            return index;
        }
    }
    return -1;
}

QString SpeechProviderSetupPage::blockedReason() const
{
    const int index = selectedIndex();
    if (index < 0) {
        return QStringLiteral("No transcription service is available.");
    }
    return QStringLiteral("%1 is no longer signed in.").arg(m_options.at(index).label);
}

QString SpeechProviderSetupPage::readySummary() const
{
    const int index = selectedIndex();
    if (index < 0) {
        return QString();
    }
    return QStringLiteral("Transcription — %1").arg(m_options.at(index).label);
}

void SpeechProviderSetupPage::setReady(bool ready)
{
    if (m_ready == ready) {
        return;
    }
    m_ready = ready;
    emit readyChanged();
}

void SpeechProviderSetupPage::selectProvider(const QString &providerId)
{
    m_settings.setSpeechProvider(providerId);
    const QList<ProviderDescriptor> providers = m_providers.speechProviders();
    const auto it = std::find_if(providers.cbegin(), providers.cend(),
                                 [&providerId](const ProviderDescriptor &provider) {
                                     return provider.id == providerId;
                                 });
    m_hint->setText(it == providers.cend() ? QString() : it->setupHint);
    m_stats->setStats(it == providers.cend() ? QVector<ProviderStat>{} : it->stats);
    m_accuracyPass->setVisible(providerId == QStringLiteral("codex"));
    showSelectedProvider();
}

void SpeechProviderSetupPage::checkProviders()
{
    const quint64 generation = ++m_checkGeneration;
    m_pendingProbes = m_options.size();
    for (int index = 0; index < m_options.size(); ++index) {
        probeProvider(index, generation);
    }
    showSelectedProvider();
}

void SpeechProviderSetupPage::probeProvider(int index, quint64 generation)
{
    ProviderOptionRow &option = m_options[index];
    // A re-probe leaves the last verdict on screen. Resetting to "Checking…"
    // would disable Next every time the user steps back onto the page.
    if (!option.probed) {
        setStatusColor(option.status, false);
        option.status->setText(QStringLiteral("Checking…"));
    }

    SpeechTranscriber *provider = m_providers.speechProvider(option.id);
    if (!provider) {
        finishProbe(index, generation,
                    {false, QStringLiteral("No transcription service is available.")});
        return;
    }

    const SpeechSettings settings = m_settings.snapshot().speech;
    std::optional<SpeechPrepareJob> job = provider->createPrepareJob(settings);
    if (!job || !job->run) {
        finishProbe(index, generation, provider->prepare(settings));
        return;
    }
    auto prepareJob = std::make_shared<SpeechPrepareJob>(std::move(*job));
    runOffThread<SpeechPrepareResult>(
        this,
        [prepareJob] { return prepareJob->run(); },
        [this, index, generation, prepareJob](const SpeechPrepareResult &result) {
            if (generation != m_checkGeneration) {
                return;
            }
            if (prepareJob->apply) {
                prepareJob->apply(result);
            }
            finishProbe(index, generation, result);
        });
}

void SpeechProviderSetupPage::finishProbe(int index,
                                          quint64 generation,
                                          const SpeechPrepareResult &result)
{
    if (generation != m_checkGeneration) {
        return;
    }
    ProviderOptionRow &option = m_options[index];
    option.probed = true;
    option.ok = result.ok;
    option.message = result.message;
    setStatusColor(option.status, result.ok);
    option.status->setText(result.ok ? QStringLiteral("Ready")
                                     : QStringLiteral("Not set up"));
    showSelectedProvider();
    if (--m_pendingProbes == 0) {
        autoSelectReadyProvider();
    }
}

void SpeechProviderSetupPage::showSelectedProvider()
{
    const int index = selectedIndex();
    if (index < 0) {
        setStatusColor(m_status, false);
        m_status->setText(QStringLiteral("No transcription service is available."));
        m_hint->hide();
        m_checkAgain->hide();
        setReady(false);
        return;
    }
    const ProviderOptionRow &option = m_options.at(index);
    if (!option.probed) {
        setStatusColor(m_status, false);
        m_status->setText(QStringLiteral("Checking…"));
        m_hint->show();
        m_checkAgain->show();
        setReady(false);
        return;
    }
    setStatusColor(m_status, option.ok);
    m_status->setText(option.ok ? QStringLiteral("%1 is ready.").arg(option.label)
                                : option.message);
    m_hint->setVisible(!option.ok);
    m_checkAgain->setVisible(!option.ok);
    setReady(option.ok);
}

void SpeechProviderSetupPage::autoSelectReadyProvider()
{
    if (m_autoSelectDone || m_userSelected) {
        return;
    }
    m_autoSelectDone = true;
    const int index = selectedIndex();
    if (index >= 0 && m_options.at(index).ok) {
        return;
    }
    // The saved service cannot transcribe but another one can: start the user
    // on the one that works rather than on a dead end.
    for (const ProviderOptionRow &option : m_options) {
        if (option.ok) {
            option.button->setChecked(true);
            return;
        }
    }
}

MicrophoneSetupPage::MicrophoneSetupPage(SettingsStore &settings,
                                         const PlatformComposition &platform,
                                         QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_platform(platform)
    , m_device(new QComboBox(this))
    , m_level(new QProgressBar(this))
    , m_status(new QLabel(this))
    , m_noInputTimer(new QTimer(this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Choose the input Speecher should record. Speak normally; setup continues once the level moves."));
    m_device->setMinimumContentsLength(28);
    m_level->setRange(0, 100);
    m_level->setValue(0);
    m_level->setFormat(QStringLiteral("Input level %p%"));
    m_status->setWordWrap(true);

    auto *form = new QGridLayout;
    form->addWidget(new QLabel(QStringLiteral("Microphone"), this), 0, 0);
    form->addWidget(m_device, 0, 1);
    form->addWidget(new QLabel(QStringLiteral("Live level"), this), 1, 0);
    form->addWidget(m_level, 1, 1);
    layout->addLayout(form);
    layout->addWidget(m_status);
    layout->addStretch();

    m_noInputTimer->setSingleShot(true);
    m_noInputTimer->setInterval(5000);
    connect(m_noInputTimer, &QTimer::timeout, this, [this] {
        if (m_inputDetected) {
            return;
        }
        m_status->setText(
            QStringLiteral("No input yet — check that the microphone isn't muted, or pick another device."));
    });

    m_input = m_platform.createAudioInput(&m_settings, this);
    connect(m_input, &AudioInput::failed, this, [this](const QString &message) {
        m_status->setText(message);
        m_level->setValue(0);
        m_noInputTimer->stop();
        // A device that just failed cannot be the one the gate was opened for.
        setInputDetected(false);
    });
    connect(m_device, &QComboBox::currentIndexChanged, this, [this] {
        m_settings.setAudioInputDeviceId(m_device->currentData().toString());
        // The gate is about the input that will actually record, so a switch
        // has to prove itself again.
        setInputDetected(false);
        if (m_active) {
            startMeter();
        }
    });
}

QString MicrophoneSetupPage::blockedReason() const
{
    return m_device->count() == 0 ? QStringLiteral("No microphone was found.")
                                  : QStringLiteral("No input has been detected.");
}

QString MicrophoneSetupPage::readySummary() const
{
    const QString device = m_device->currentText();
    return device.isEmpty() ? QString()
                            : QStringLiteral("Microphone — %1").arg(device);
}

void MicrophoneSetupPage::setInputDetected(bool detected)
{
    if (m_inputDetected == detected) {
        return;
    }
    m_inputDetected = detected;
    emit inputDetectedChanged();
}

MicrophoneSetupPage::~MicrophoneSetupPage()
{
    if (m_input) {
        m_input->stop();
    }
}

void MicrophoneSetupPage::setActive(bool active)
{
    if (m_active == active) {
        return;
    }
    m_active = active;
    if (active) {
        if (isVisible()) {
            if (!m_devicesLoaded) {
                refreshDevices();
                m_devicesLoaded = true;
            }
            startMeter();
        }
    } else if (m_input) {
        m_input->stop();
        m_level->setValue(0);
        m_noInputTimer->stop();
    }
}

void MicrophoneSetupPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (!m_devicesLoaded) {
        refreshDevices();
        m_devicesLoaded = true;
    }
    if (m_active) {
        startMeter();
    }
}

void MicrophoneSetupPage::refreshDevices()
{
    settings::populateAudioInputDevices(m_device,
                                        m_platform.availableAudioInputDevices(),
                                        m_settings.audioInputDeviceId());
}

void MicrophoneSetupPage::startMeter()
{
    // Retire the old handler before stopping: stop() spins a nested event loop
    // to drain the post-roll, which delivers more of the previous device's
    // levels. Those belong to the run that is ending, not the one about to
    // start, so the generation must already have moved on.
    disconnect(m_levelConnection);
    const quint64 generation = ++m_meterGeneration;
    m_input->stop();
    m_level->setValue(0);
    m_noInputTimer->stop();
    m_levelConnection = connect(m_input, &AudioInput::levelChanged, this,
                                [this, generation](float level) {
        if (generation != m_meterGeneration) {
            return;
        }
        m_level->setValue(qBound(0, qRound(level * 100.0f), 100));
        if (level > 0.01f) {
            m_noInputTimer->stop();
            m_status->setText(QStringLiteral("Microphone input detected."));
            setInputDetected(true);
        }
    });

    QString error;
    if (!m_input->start(&error)) {
        m_status->setText(error);
        return;
    }
    m_status->setText(QStringLiteral("Listening for microphone input…"));
    m_noInputTimer->start();
}

AccessibilitySetupPage::AccessibilitySetupPage(ApplicationController &controller,
                                               QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_status(new QLabel(this))
#ifdef Q_OS_WIN
    , m_enable(new QPushButton(QStringLiteral("No action needed"), this))
#else
    , m_enable(new QPushButton(QStringLiteral("Enable permanently"), this))
#endif
{
    QVBoxLayout *layout = makePage(
        this,
#ifdef Q_OS_WIN
        QStringLiteral("Windows UI Automation lets Speecher identify the target app, read nearby text, and learn corrections. It does not require a permission grant."));
#else
        QStringLiteral("Speecher pastes your dictation into the app you are using, and reads the text around your cursor so cleanup understands the context. On Linux both work through the desktop accessibility service (AT-SPI); it also lets Speecher learn your corrections."));
#endif
    m_status->setWordWrap(true);
    m_enable->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
#ifdef Q_OS_WIN
    layout->addWidget(m_status);
    m_enable->hide();
#else
    // The permission buys exactly two things, so each is named with its own
    // verdict rather than left inside one sentence about "accessibility".
    QFormLayout *card = addCard(layout, this, QString());
    QWidget *host = card->parentWidget();
    for (const QString &capability : {QStringLiteral("Paste into the app you are using"),
                                      QStringLiteral("Read the text around your cursor for context")}) {
        const StatusRow row = makeStatusRow(host, nullptr, capability, false);
        settings::addCardRow(card, row.widget, host);
        m_capabilities.append(row.status);
    }
    layout->addWidget(m_status);
    layout->addWidget(m_enable, 0, Qt::AlignLeft);
    auto *reassurance = new QLabel(
        QStringLiteral("This only affects this app's ability to type for you. You can turn it off any time in Settings."),
        this);
    reassurance->setWordWrap(true);
    reassurance->setFont(settings::smallFont(reassurance->font()));
    reassurance->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(reassurance);
    connect(m_enable, &QPushButton::clicked, this, [this] {
        m_lastError.clear();
        QString error;
        if (!m_controller.enableAccessibility(&error)) {
            m_lastError = error;
        }
        // enableAccessibility() refreshes the state itself, but it did so before
        // the error existed.
        refreshFromController();
    });
#endif
    layout->addStretch();
    connect(&m_controller,
            &ApplicationController::accessibilityStateChanged,
            this,
            &AccessibilitySetupPage::updateState);
    refreshFromController();
}

void AccessibilitySetupPage::refreshFromController()
{
    updateState(m_controller.accessibilitySupported(),
                m_controller.accessibilityEnabled(),
                m_controller.accessibilityPersistent());
}

bool AccessibilitySetupPage::stepComplete() const
{
#ifdef Q_OS_WIN
    return true;
#else
    return !m_supported || m_enabled;
#endif
}

void AccessibilitySetupPage::updateState(bool supported, bool enabled, bool persistent)
{
    const bool wasComplete = stepComplete();
    m_supported = supported;
    m_enabled = enabled;
    QString status;
#ifdef Q_OS_WIN
    Q_UNUSED(persistent);
    status = QStringLiteral("UI Automation is available. No permission grant is needed.");
    m_enable->hide();
#else
    if (!supported) {
        status = QStringLiteral("This Speecher build does not include desktop accessibility support.");
        m_enable->setEnabled(false);
        m_enable->setText(QStringLiteral("Unavailable"));
    } else if (enabled && persistent) {
        status = QStringLiteral("Desktop accessibility is enabled permanently.");
        m_enable->setEnabled(false);
        m_enable->setText(QStringLiteral("Enabled"));
    } else if (enabled) {
        status = QStringLiteral("Desktop accessibility is enabled for this session only.");
        m_enable->setEnabled(true);
        m_enable->setText(QStringLiteral("Enable permanently"));
    } else {
        status = QStringLiteral("Accessibility is off, so Speecher can copy your dictation but not paste it or see context. Turn it on to continue.");
        m_enable->setEnabled(true);
        m_enable->setText(QStringLiteral("Enable permanently"));
    }
    showCapabilities(enabled);
#endif
    m_status->setText(m_lastError.isEmpty() ? status : m_lastError);
    if (wasComplete != stepComplete()) {
        emit stepCompleteChanged();
    }
}

void AccessibilitySetupPage::showCapabilities(bool allowed)
{
    for (QLabel *capability : m_capabilities) {
        setStatusColor(capability, allowed);
        capability->setText(allowed ? QStringLiteral("Allowed")
                                    : QStringLiteral("Blocked"));
    }
}

QString AccessibilitySetupPage::blockedReason() const
{
    return QStringLiteral("Accessibility is off, so Speecher cannot paste or read context.");
}

TextDeliverySetupPage::TextDeliverySetupPage(SettingsStore &settings, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_status(new WrappingLabel(this))
    , m_setup(new QPushButton(QStringLiteral("Set up virtual keyboard"), this))
    , m_progress(new QProgressBar(this))
    , m_clipboardOnly(new QCheckBox(
          QStringLiteral("Continue without the virtual keyboard; Speecher pastes from the clipboard instead"),
          this))
    , m_restoreClipboard(new QCheckBox(restoreClipboardDescription(), this))
    , m_format(new QComboBox(this))
{
    QVBoxLayout *layout = makePage(
        this,
#ifdef Q_OS_WIN
        QStringLiteral("Speecher puts the finished text on your clipboard and pastes it into the frontmost app with Ctrl+V. Nothing extra needs to be installed."));
#else
        QStringLiteral("To type for you, Speecher installs a small virtual keyboard. Your computer will ask for your administrator password once; Speecher itself never runs privileged."));
#endif
    m_status->setWordWrap(true);
    m_progress->setRange(0, 0);
    m_progress->setVisible(false);
    m_format->addItem(QStringLiteral("Plain text"), QStringLiteral("plain"));
    m_format->addItem(QStringLiteral("HTML and plain text"), QStringLiteral("html"));
    m_restoreClipboard->setChecked(m_settings.restoreClipboardAfterTyping());
    settings::selectData(m_format, outputFormatName(m_settings.outputFormat()));

    // The state and the button that changes it read as one block, with the
    // choices that follow from it under the same frame.
    QFormLayout *card = addCard(layout, this, QString());
    QWidget *host = card->parentWidget();

    auto *keyboardRow = new QWidget(host);
    auto *keyboardLayout = new QVBoxLayout(keyboardRow);
    keyboardLayout->setContentsMargins(settings::rowPadding());
    keyboardLayout->setSpacing(settings::smallSpacing());
    m_status->setParent(keyboardRow);
    keyboardLayout->addWidget(m_status);
    m_progress->setParent(keyboardRow);
    keyboardLayout->addWidget(m_progress);
    m_setup->setParent(keyboardRow);
    m_setup->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    keyboardLayout->addWidget(m_setup, 0, Qt::AlignLeft);
    settings::addCardRow(card, keyboardRow, host);

    // QCheckBox does not wrap its own label, and both of these are sentences:
    // the settings row pairs a wrapping caption with an unlabelled box.
    m_clipboardOnlyRow = settings::makeRow(
        QStringLiteral("Continue without the virtual keyboard; Speecher pastes from the clipboard instead"),
        QString(),
        m_clipboardOnly,
        host);
    settings::addCardRow(card, m_clipboardOnlyRow, host);
    settings::addCardRow(
        card,
        settings::makeRow(QStringLiteral("Clipboard format"), QString(), m_format, host),
        host);
    settings::addCardRow(
        card,
        settings::makeRow(restoreClipboardDescription(), QString(), m_restoreClipboard, host),
        host);
    layout->addStretch();
#ifndef SPEECHER_WITH_YDOTOOL
    // Nothing to install and nothing to opt out of.
    setCardRowVisible(m_clipboardOnlyRow, false);
#endif

    connect(m_setup, &QPushButton::clicked, this, &TextDeliverySetupPage::runSetup);
    connect(m_clipboardOnly, &QCheckBox::toggled, this, [this] {
        emit stepCompleteChanged();
    });
    connect(m_restoreClipboard, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.setRestoreClipboardAfterTyping(checked);
    });
    connect(m_format, &QComboBox::currentIndexChanged, this, [this] {
        m_settings.setOutputFormat(outputFormatFromString(m_format->currentData().toString()));
    });
    refreshStatus();
}

bool TextDeliverySetupPage::needsSignIn() const
{
#ifdef SPEECHER_WITH_YDOTOOL
    return YdotoolSetup::probe(m_settings.ydotoolEnabled()).state
        == YdotoolSetupState::NeedsSignOut;
#else
    return false;
#endif
}

QString TextDeliverySetupPage::blockedReason() const
{
    return QStringLiteral(
        "The virtual keyboard is not set up, so Speecher cannot type into other apps.");
}

QString TextDeliverySetupPage::readySummary() const
{
#ifdef SPEECHER_WITH_YDOTOOL
    if (!m_clipboardOnly->isChecked()) {
        return QStringLiteral("Text delivery — virtual keyboard");
    }
#endif
    return QStringLiteral("Text delivery — clipboard");
}

bool TextDeliverySetupPage::stepComplete() const
{
#ifdef SPEECHER_WITH_YDOTOOL
    if (m_clipboardOnly->isChecked()) {
        return true;
    }
    // ready() already implies enabled in Speecher; NeedsSignOut is as far as
    // this session can get, the enable step waits in the Output settings
    // after the next sign-in.
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_settings.ydotoolEnabled());
    return status.ready() || status.state == YdotoolSetupState::NeedsSignOut;
#else
    return true;
#endif
}

#ifdef SPEECHER_WITH_YDOTOOL
void TextDeliverySetupPage::refreshStatus()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_settings.ydotoolEnabled());
    const bool needsSignIn = status.state == YdotoolSetupState::NeedsSignOut;
    m_status->setText(needsSignIn
                          ? QStringLiteral("Almost done — log out of your computer and back in, then turn on the virtual keyboard in Settings > Output.")
                          : status.label + QStringLiteral(". ") + status.detail);
    m_setup->setEnabled(!status.ready() && !needsSignIn);
    m_setup->setText(status.ready() ? QStringLiteral("Virtual keyboard ready")
                                    : QStringLiteral("Set up virtual keyboard"));
    // With a working virtual keyboard there is nothing to opt out of.
    setCardRowVisible(m_clipboardOnlyRow, !status.ready());
}

void TextDeliverySetupPage::runSetup()
{
    m_setup->setEnabled(false);
    m_progress->setVisible(true);
    m_status->setText(QStringLiteral("Setting up the virtual keyboard…"));
    if (!startYdotoolSetup(
            m_settings,
            this,
            YdotoolSetupFlowOptions{
                .confirmInstall = true,
                .applyAutomaticOutputMethod = true,
            },
            this,
            [this](const YdotoolSetupFlowResult &result) {
            m_progress->setVisible(false);
            if (!result.helperOk) {
                m_status->setText(
                    QStringLiteral("Setup failed: %1").arg(result.helperError));
                m_setup->setEnabled(true);
                return;
            }

            refreshStatus();
            if (!result.serviceError.isEmpty()) {
                if (result.status.state == YdotoolSetupState::NeedsSignOut) {
                    m_status->setText(
                        QStringLiteral("Almost done — log out of your computer and back in, then turn on the virtual keyboard in Settings > Output. The service could not start: %1")
                            .arg(result.serviceError));
                } else {
                    m_status->setText(
                        QStringLiteral("Setup installed, but the service could not start: %1")
                            .arg(result.serviceError));
                    if (!result.status.ready()) {
                        m_setup->setEnabled(true);
                    }
                }
            }
            emit signInRequirementChanged(needsSignIn());
            emit stepCompleteChanged();
        })) {
        m_progress->setVisible(false);
        refreshStatus();
        emit stepCompleteChanged();
    }
}
#else
// Keyboard paste needs no user-installed helper off Linux, so the page keeps
// only the clipboard controls, which are portable.
void TextDeliverySetupPage::refreshStatus()
{
    m_status->setText(
#ifdef Q_OS_WIN
        QStringLiteral("Nothing to install — Speecher uses the Ctrl+V paste built into Windows."));
#else
        QStringLiteral("Nothing to install — Speecher pastes with the system clipboard."));
#endif
    m_setup->setVisible(false);
    m_progress->setVisible(false);
}

void TextDeliverySetupPage::runSetup() {}
#endif

RefinementSetupPage::RefinementSetupPage(SettingsStore &settings,
                                         ProviderRegistry &providers,
                                         QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_none(nullptr)
    , m_stats(new ProviderStatsBlock(this))
    , m_warning(new WrappingLabel(this))
    , m_fastMode(new QCheckBox(QStringLiteral("Fast mode"), this))
    , m_fastModeHint(new QLabel(this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Refinement can clean up a raw transcript after dictation. Choose a provider, or None to skip cleanup."));

    QFormLayout *choices = addCard(layout, this, QStringLiteral("Cleanup provider"));
    QWidget *host = choices->parentWidget();
    auto *group = new QButtonGroup(this);
    const QString savedProvider = m_settings.refinementProvider();
    for (const ProviderDescriptor &provider : providers.refinementProviders()) {
        // The brands differ from the transcription page's, so say which
        // sign-in each one actually uses.
        m_options.append(addOptionRow(choices, group, provider.id, provider.label,
                                      credentialNote(provider),
                                      QStringLiteral("refinementProvider")));
        m_options.last().button->setChecked(provider.id == savedProvider);
    }

    const ProviderOptionRow none = addOptionRow(choices, group,
                                                QStringLiteral("none"),
                                                QStringLiteral("None"),
                                                QStringLiteral("Skip cleanup entirely."),
                                                QStringLiteral("refinementProvider"));
    m_none = none.button;
    none.status->setText(QStringLiteral("No cleanup"));
    m_none->setChecked(selectedIndex() < 0);

    m_warning->setObjectName(QStringLiteral("refinementProviderWarning"));
    m_warning->setWordWrap(true);
    m_warning->hide();
    // Directly under the cards, so it reads as attached to the selection above
    // it rather than to the facts below.
    layout->addWidget(m_warning);
    layout->addWidget(m_stats);
    m_fastMode->setObjectName(QStringLiteral("refinementFastMode"));
    m_fastModeHint->setWordWrap(true);
    layout->addWidget(m_fastMode);
    layout->addWidget(m_fastModeHint);
    layout->addStretch();

    for (const ProviderOptionRow &option : m_options) {
        const QString providerId = option.id;
        connect(option.button, &QRadioButton::clicked, this, [this] { m_userSelected = true; });
        connect(option.button, &QRadioButton::toggled, this, [this, providerId](bool checked) {
            if (checked) {
                selectProvider(providerId);
            }
        });
    }
    connect(m_none, &QRadioButton::clicked, this, [this] { m_userSelected = true; });
    connect(m_none, &QRadioButton::toggled, this, [this](bool checked) {
        if (checked) {
            selectProvider(QStringLiteral("none"));
        }
    });
    connect(m_fastMode, &QCheckBox::toggled, this, [this](bool checked) {
        const QString provider = selectedProviderId();
        if (provider == QStringLiteral("openai")) {
            m_settings.setOpenAiFastMode(checked);
        } else if (provider == QStringLiteral("anthropic")) {
            m_settings.setAnthropicFastMode(checked);
        }
    });
    selectProvider(selectedProviderId());
}

void RefinementSetupPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Same reasoning as the transcription page: probe when the page is shown,
    // not while the wizard is building, so the credential reads serialize and
    // a mid-wizard sign-in is picked up.
    checkProviders();
}

int RefinementSetupPage::selectedIndex() const
{
    for (int index = 0; index < m_options.size(); ++index) {
        if (m_options.at(index).button->isChecked()) {
            return index;
        }
    }
    return -1;
}

QString RefinementSetupPage::readySummary() const
{
    const int index = selectedIndex();
    return QStringLiteral("Refinement — %1")
        .arg(index < 0 ? QStringLiteral("None") : m_options.at(index).label);
}

QString RefinementSetupPage::selectedProviderId() const
{
    const int index = selectedIndex();
    return index < 0 ? QStringLiteral("none") : m_options.at(index).id;
}

void RefinementSetupPage::selectProvider(const QString &providerId)
{
    m_settings.setRefinementProvider(providerId);
    updateProviderStats();
    updateFastModeControl();
    showSelectedProvider();
}

void RefinementSetupPage::checkProviders()
{
    const quint64 generation = ++m_checkGeneration;
    m_pendingProbes = m_options.size();
    for (int index = 0; index < m_options.size(); ++index) {
        probeProvider(index, generation);
    }
    showSelectedProvider();
    if (m_options.isEmpty()) {
        autoSelectReadyProvider();
    }
}

void RefinementSetupPage::probeProvider(int index, quint64 generation)
{
    ProviderOptionRow &option = m_options[index];
    // A re-probe keeps the last verdict on screen rather than flashing back to
    // "Checking…" every time the user returns to the page.
    if (!option.probed) {
        setStatusColor(option.status, false);
        option.status->setText(QStringLiteral("Checking…"));
    }

    TranscriptRefiner *refiner = m_providers.refinementProvider(option.id);
    if (!refiner) {
        finishProbe(index, generation, {false, QStringLiteral("Not available.")});
        return;
    }

    const RefinementSettings settings = m_settings.snapshot().refinement;
    std::optional<RefinementRefreshJob> job = refiner->createRefreshJob(settings);
    if (!job || !job->run) {
        finishProbe(index, generation, refiner->prepare(settings));
        return;
    }
    auto refreshJob = std::make_shared<RefinementRefreshJob>(std::move(*job));
    runOffThread<RefinementRefreshResult>(
        this,
        [refreshJob] { return refreshJob->run(); },
        [this, index, generation, refreshJob](const RefinementRefreshResult &result) {
            if (generation != m_checkGeneration) {
                return;
            }
            if (refreshJob->apply) {
                refreshJob->apply(result);
            }
            finishProbe(index, generation, {result.ok, result.message});
        });
}

void RefinementSetupPage::finishProbe(int index,
                                      quint64 generation,
                                      const RefinementPrepareResult &result)
{
    if (generation != m_checkGeneration) {
        return;
    }
    ProviderOptionRow &option = m_options[index];
    option.probed = true;
    option.ok = result.ok;
    option.message = result.message;
    setStatusColor(option.status, result.ok);
    option.status->setText(result.ok ? QStringLiteral("Ready")
                                     : QStringLiteral("Not set up"));
    showSelectedProvider();
    if (--m_pendingProbes == 0) {
        autoSelectReadyProvider();
    }
}

void RefinementSetupPage::showSelectedProvider()
{
    const int index = selectedIndex();
    if (index < 0) {
        m_warning->hide();
        return;
    }
    const ProviderOptionRow &option = m_options.at(index);
    // Refinement stays optional, so an unready provider is a warning rather
    // than a gate: dictation still delivers, just without the cleanup.
    const bool warn = option.probed && !option.ok;
    m_warning->setVisible(warn);
    if (warn) {
        setStatusColor(m_warning, false);
        m_warning->setText(
            QStringLiteral("%1 is not signed in. Dictation will deliver the raw transcript.")
                .arg(option.label));
    }
}

void RefinementSetupPage::autoSelectReadyProvider()
{
    if (m_autoSelectDone || m_userSelected) {
        return;
    }
    m_autoSelectDone = true;
    const int index = selectedIndex();
    if (index < 0 || m_options.at(index).ok) {
        return;
    }
    for (const ProviderOptionRow &option : m_options) {
        if (option.ok) {
            option.button->setChecked(true);
            return;
        }
    }
}

void RefinementSetupPage::updateProviderStats()
{
    const QString providerId = selectedProviderId();
    const QList<ProviderDescriptor> providers = m_providers.refinementProviders();
    const auto it = std::find_if(providers.cbegin(), providers.cend(),
                                 [&providerId](const ProviderDescriptor &provider) {
                                     return provider.id == providerId;
                                 });
    m_stats->setStats(it == providers.cend() ? QVector<ProviderStat>{} : it->stats);
}

void RefinementSetupPage::updateFastModeControl()
{
    const QString provider = selectedProviderId();
    const bool openAi = provider == QStringLiteral("openai");
    const bool anthropic = provider == QStringLiteral("anthropic");
    m_fastMode->setVisible(openAi || anthropic);
    m_fastModeHint->setVisible(openAi || anthropic);
    if (!openAi && !anthropic) {
        return;
    }
    m_fastModeHint->setText(openAi
                                ? QStringLiteral("1.5x speed and increased usage (negligible).")
                                : QStringLiteral("Faster refinement will use usage credits."));
    m_fastMode->setToolTip(openAi
                               ? QStringLiteral("Falls back to standard processing when a fast request fails.")
                               : QStringLiteral("Only Opus models support fast mode; other models refine at standard speed."));
    const QSignalBlocker blocker(m_fastMode);
    m_fastMode->setChecked(openAi ? m_settings.openAiFastMode() : m_settings.anthropicFastMode());
}

WritingProfilesSetupPage::WritingProfilesSetupPage(SettingsStore &settings, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_defaultProfile(new QComboBox(this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Choose the fallback Writing Profile and how much cleanup and tone adjustment each profile receives."));
    addProfiles(m_defaultProfile);
    settings::selectData(m_defaultProfile, m_settings.defaultWritingProfile());

    auto *grid = new QGridLayout;
    grid->addWidget(new QLabel(QStringLiteral("Default profile"), this), 0, 0);
    grid->addWidget(m_defaultProfile, 0, 1, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("Profile"), this), 2, 0);
    grid->addWidget(new QLabel(QStringLiteral("Cleanup"), this), 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("Tone"), this), 2, 2);

    int row = 3;
    const QList<WritingProfileSettings> current = m_settings.writingProfileSettings();
    for (const WritingProfileSettings &fallback : defaultWritingProfileSettings()) {
        const WritingProfileSettings saved = writingProfileSettingsFor(current, fallback.profile);
        auto *cleanup = new QComboBox(this);
        auto *tone = new QComboBox(this);
        addCleanupLevels(cleanup);
        addTones(tone);
        settings::selectData(cleanup, saved.cleanupStrength);
        settings::selectData(tone, saved.tone);
        grid->addWidget(new QLabel(profileLabel(fallback.profile), this), row, 0);
        grid->addWidget(cleanup, row, 1);
        grid->addWidget(tone, row, 2);
        m_profiles.append({fallback.profile, cleanup, tone});
        connect(cleanup, &QComboBox::currentIndexChanged,
                this, &WritingProfilesSetupPage::saveProfiles);
        connect(tone, &QComboBox::currentIndexChanged,
                this, &WritingProfilesSetupPage::saveProfiles);
        ++row;
    }
    layout->addLayout(grid);
    auto *note = new WrappingLabel(
        QStringLiteral("The default profile is used when Speecher does not recognise the app you are dictating into. Every profile can be changed later in Settings."),
        this);
    note->setWordWrap(true);
    note->setFont(settings::smallFont(note->font()));
    note->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(note);
    layout->addStretch();
    connect(m_defaultProfile, &QComboBox::currentIndexChanged, this, [this] {
        m_settings.setDefaultWritingProfile(m_defaultProfile->currentData().toString());
    });
}

void WritingProfilesSetupPage::saveProfiles()
{
    QList<WritingProfileSettings> profiles;
    for (const ProfileControls &controls : m_profiles) {
        profiles.append({
            controls.profile,
            controls.cleanup->currentData().toString(),
            controls.tone->currentData().toString(),
        });
    }
    m_settings.setWritingProfileSettings(profiles);
}

FinishSetupPage::FinishSetupPage(ApplicationController &controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_intro(nullptr)
    , m_shortcutStatus(new QLabel(this))
    , m_signInNote(new QLabel(this))
    , m_completed(new QWidget(this))
    , m_blocked(new QWidget(this))
{
    QVBoxLayout *layout = makePage(this, QStringLiteral("Setup is complete."), &m_intro);
    setStatusColor(m_intro, true);
    m_shortcutStatus->setWordWrap(true);
    m_shortcutStatus->setObjectName(QStringLiteral("finishGlobalShortcutStatus"));
    m_signInNote->setWordWrap(true);

#ifdef Q_OS_LINUX
    m_trayNote = new QLabel(this);
    m_trayNote->setWordWrap(true);
    m_trayNote->setObjectName(QStringLiteral("finishTrayNote"));
    m_manualCommand = new QLabel(this);
    m_manualCommand->setObjectName(QStringLiteral("finishGlobalShortcutCommand"));
    m_manualCommand->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_manualCommand->setTextInteractionFlags(Qt::TextSelectableByMouse
                                             | Qt::TextSelectableByKeyboard);
    connect(&m_controller,
            &ApplicationController::globalShortcutChanged,
            this,
            [this] { updateLinuxShortcutInstruction(); });
    updateLinuxShortcutInstruction();
#else
    m_shortcutStatus->setText(
        QStringLiteral("Use your Global Shortcut to start and stop dictation."));
#endif

    // What the user got, once every step is done.
    auto *completedLayout = new QVBoxLayout(m_completed);
    completedLayout->setContentsMargins(0, 0, 0, 0);
    completedLayout->setSpacing(settings::largeSpacing());
    QFormLayout *howTo = addCard(completedLayout, m_completed,
                                 QStringLiteral("How to dictate"));
    QWidget *howToHost = howTo->parentWidget();
    auto *instruction = new QWidget(howToHost);
    auto *instructionLayout = new QVBoxLayout(instruction);
    instructionLayout->setContentsMargins(settings::rowPadding());
    instructionLayout->setSpacing(settings::smallSpacing());
    m_shortcutStatus->setParent(instruction);
    instructionLayout->addWidget(m_shortcutStatus);
#ifdef Q_OS_LINUX
    m_manualCommand->setParent(instruction);
    m_manualCommand->setWordWrap(true);
    instructionLayout->addWidget(m_manualCommand);
    m_trayNote->setParent(instruction);
    m_trayNote->setFont(settings::smallFont(m_trayNote->font()));
    m_trayNote->setForegroundRole(QPalette::PlaceholderText);
    instructionLayout->addWidget(m_trayNote);
#endif
    settings::addCardRow(howTo, instruction, howToHost);
    m_completedList = new QWidget(m_completed);
    auto *completedListLayout = new QVBoxLayout(m_completedList);
    completedListLayout->setContentsMargins(0, 0, 0, 0);
    completedListLayout->setSpacing(0);
    completedLayout->addWidget(m_completedList);
    m_signInNote->setParent(m_completed);
    completedLayout->addWidget(m_signInNote);
    layout->addWidget(m_completed);

    // What is left, when a step broke or was never finished. Finish stays
    // disabled either way; this says which step to go back to and why.
    auto *blockedLayout = new QVBoxLayout(m_blocked);
    blockedLayout->setContentsMargins(0, 0, 0, 0);
    blockedLayout->setSpacing(settings::largeSpacing());
    blockedLayout->addWidget(settings::makeSectionLabel(
        QStringLiteral("A few steps still need attention:"), m_blocked));
    m_blockedList = new QWidget(m_blocked);
    auto *blockedListLayout = new QVBoxLayout(m_blockedList);
    blockedListLayout->setContentsMargins(0, 0, 0, 0);
    blockedListLayout->setSpacing(0);
    blockedLayout->addWidget(m_blockedList);
    auto *blockedFoot = new WrappingLabel(
        QStringLiteral("Finish becomes available once every step above is resolved."),
        m_blocked);
    blockedFoot->setWordWrap(true);
    blockedFoot->setFont(settings::smallFont(blockedFoot->font()));
    blockedFoot->setForegroundRole(QPalette::PlaceholderText);
    blockedLayout->addWidget(blockedFoot);
    m_blocked->hide();
    layout->addWidget(m_blocked);

    layout->addStretch();
}

void FinishSetupPage::setSteps(const QList<SetupStepStatus> &steps)
{
    const bool blocked = std::any_of(steps.cbegin(), steps.cend(),
                                     [](const SetupStepStatus &step) { return !step.ok; });
    m_intro->setText(blocked
        ? QStringLiteral("Speecher can't dictate yet. Finish the steps below, or go back and change your choices.")
        : QStringLiteral("Setup is complete."));
    setStatusColor(m_intro, !blocked);
    m_completed->setVisible(!blocked);
    m_blocked->setVisible(blocked);
    if (blocked) {
        showBlockedSteps(steps);
    } else {
        showCompletedSteps(steps);
    }
}

void FinishSetupPage::showBlockedSteps(const QList<SetupStepStatus> &steps)
{
    // The list is rebuilt rather than patched: which steps appear, and in what
    // order, changes every time a gate opens or closes.
    qDeleteAll(m_blockedList->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
    auto *layout = qobject_cast<QVBoxLayout *>(m_blockedList->layout());
    QFormLayout *card = addCard(layout, m_blockedList, QString());
    QWidget *host = card->parentWidget();
    for (int index = 0; index < steps.size(); ++index) {
        const SetupStepStatus &step = steps.at(index);
        if (step.ok) {
            continue;
        }
        auto *goToStep = new QPushButton(QStringLiteral("Go to step"), host);
        connect(goToStep, &QPushButton::clicked, this, [this, index] {
            emit stepSelected(index);
        });
        const StatusRow row = makeStatusRow(
            host,
            makeGlyph(host, QStringLiteral("dialog-warning"), QStyle::SP_MessageBoxWarning),
            step.name,
            true,
            goToStep);
        row.hint->setText(step.detail);
        row.hint->setVisible(!step.detail.isEmpty());
        settings::addCardRow(card, row.widget, host);
    }
    showRebuiltList(m_blockedList);
}

void FinishSetupPage::showCompletedSteps(const QList<SetupStepStatus> &steps)
{
    qDeleteAll(m_completedList->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly));
    auto *layout = qobject_cast<QVBoxLayout *>(m_completedList->layout());
    QFormLayout *card = nullptr;
    for (const SetupStepStatus &step : steps) {
        // Only the steps where something was chosen have anything to report.
        if (step.detail.isEmpty()) {
            continue;
        }
        if (!card) {
            card = addCard(layout, m_completedList, QString());
        }
        QWidget *host = card->parentWidget();
        const StatusRow row = makeStatusRow(host, nullptr, step.detail, false);
        setStatusColor(row.status, true);
        row.status->setText(QStringLiteral("Ready"));
        settings::addCardRow(card, row.widget, host);
    }
    showRebuiltList(m_completedList);
}

void FinishSetupPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
#ifdef Q_OS_LINUX
    updateLinuxShortcutInstruction();
#endif
}

#ifdef Q_OS_LINUX
void FinishSetupPage::updateLinuxShortcutInstruction()
{
    const QString display = m_controller.globalShortcutDisplay();
    if (!display.isEmpty()) {
        m_shortcutStatus->setText(
            QStringLiteral("To dictate, %1.")
                .arg(activationInstruction(m_controller.settings()->shortcutActivationMode(),
                                           display)));
        m_manualCommand->hide();
        m_trayNote->setText(
            linuxTrayShortcutNote(QSystemTrayIcon::isSystemTrayAvailable()));
        m_trayNote->show();
        return;
    }
    m_shortcutStatus->setText(
        m_controller.globalShortcutsSupported()
            ? QStringLiteral(
                  "No Global Shortcut is set yet. Go back to set one, or bind this command yourself:")
            : linuxGlobalShortcutManualInstruction());
    m_manualCommand->setText(linuxGlobalShortcutCommand());
    m_manualCommand->show();
    // Both fallbacks recommend the manual command, which starts Speecher when
    // it is not already running, so the running-app caveat does not apply.
    m_trayNote->hide();
}
#endif

void FinishSetupPage::setSignInRequired(bool required)
{
    m_signInNote->setVisible(required);
    m_signInNote->setText(required
                              ? QStringLiteral("Sign out and back in so the new group membership takes effect, then enable the virtual keyboard in the Output settings.")
                              : QString());
}

} // namespace speecher
