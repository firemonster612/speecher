#include "ui/setup/SetupPages.h"

#include "app/AccessibilityPresentation.h"
#include "app/ApplicationController.h"
#include "app/LocalSetup.h"
#include "app/PlatformComposition.h"
#include "app/SetupSteps.h"
#include "core/SettingsStore.h"
#include "core/settings/SettingsSchema.h"
#include "dictation/DictationPorts.h"
#include "dictation/DictationTypes.h"
#include "frontend/qt/LocalModelRows.h"
#ifdef SPEECHER_WITH_YDOTOOL
#include "output/YdotoolSetup.h"
#include "output/YdotoolSetupFlow.h"
#endif
#include "providers/ProviderProbe.h"
#include "providers/CustomEndpoints.h"
#include "providers/LocalModelStore.h"
#include "providers/ProviderRegistry.h"
#include "ui/InlineMessage.h"
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
#include <QDesktopServices>
#include <QHash>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalBlocker>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
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

using settings::setCardRowVisible;
using settings::WrappingLabel;

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

// The provider's own mark; a themed glyph for the choices that run on this
// computer or a person's own server; nothing for any other provider.
QLabel *makeProviderMark(const QString &providerId, QWidget *parent)
{
    static const QHash<QString, QString> glyphs{
        {QStringLiteral("local"), QStringLiteral("computer")},
        {QStringLiteral("endpoint"), QStringLiteral("network-server")},
    };
    QPixmap mark = providerMark(providerId, markSize(), parent->devicePixelRatioF());
    if (mark.isNull() && glyphs.contains(providerId)) {
        mark = QIcon::fromTheme(glyphs.value(providerId)).pixmap(QSize(markSize(), markSize()),
                                                                 parent->devicePixelRatioF());
    }
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
    // A wrapped label still reports a wide minimum, and on a row with a mark
    // that minimum pushes the status past the card edge, clipping it. The name
    // is the one column that can give way: let it compress and wrap instead.
    row.name->setMinimumWidth(1);
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

void addOptions(QComboBox *combo, const QList<RowOption> &options)
{
    for (const RowOption &option : options) {
        combo->addItem(option.label, option.id);
    }
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

// A small grey line under something, indented to start where its text does.
QLabel *makeNote(const QString &text, QWidget *parent, int indent = 0)
{
    auto *note = new WrappingLabel(text, parent);
    note->setWordWrap(true);
    note->setFont(settings::smallFont(note->font()));
    note->setForegroundRole(QPalette::PlaceholderText);
    note->setContentsMargins(indent, 0, 0, 0);
    return note;
}

// A themed glyph at mark size beside a line of text.
QWidget *makeGlyphLine(QWidget *parent, const QString &iconName, QLabel **textOut)
{
    auto *line = new QWidget(parent);
    auto *layout = new QHBoxLayout(line);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(settings::relatedSpacing());
    auto *glyph = new QLabel(line);
    glyph->setPixmap(QIcon::fromTheme(iconName).pixmap(settings::smallFont(line->font()).pointSize() + 6));
    layout->addWidget(glyph, 0, Qt::AlignTop);
    auto *text = new WrappingLabel(line);
    text->setWordWrap(true);
    layout->addWidget(text, 1);
    *textOut = text;
    return line;
}

} // namespace

WelcomeSetupPage::WelcomeSetupPage(QWidget *parent)
    : QWidget(parent)
{
    makePage(this, findSetupStep(QStringLiteral("welcome"))->intro)->addStretch();
}

SpeechProviderSetupPage::SpeechProviderSetupPage(SettingsStore &settings,
                                                 ProviderRegistry &providers,
                                                 LocalSetup *local,
                                                 QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_local(local)
    , m_signIn(settings)
    , m_accuracyPass(new QCheckBox(this))
    , m_stats(new ProviderStatsBlock(this))
    , m_hint(new WrappingLabel(this))
    , m_status(new WrappingLabel(this))
    , m_checkAgain(new QPushButton(setupText(SetupText::CheckAgain), this))
{
    QVBoxLayout *layout = makePage(this, findSetupStep(QStringLiteral("transcription"))->intro);

    // Core words the dead end and decides when it shows.
    m_deadEnd = new InlineMessage(this);
    m_deadEnd->setObjectName(QStringLiteral("speechDeadEnd"));
    m_deadEnd->setType(InlineMessage::Type::Warning);
    m_deadEnd->setCloseButtonVisible(false);
    m_deadEnd->hide();
    layout->addWidget(m_deadEnd);

    // Every service is on the page with its own readiness, so the choice does
    // not hide behind a dropdown the user has to open to find it.
    QFormLayout *choices = addCard(layout, this, setupText(SetupText::TranscriptionService));
    auto *group = new QButtonGroup(this);
    const QString savedProvider = m_settings.speechProvider();
    for (const ProviderDescriptor &provider : m_providers.speechProviders()) {
        // The Local card is only a choice where the assistant can set it up,
        // and a speech server is set up in Settings alone.
        if (!offersSetupSpeechProvider(provider.id, savedProvider, m_local != nullptr)) {
            continue;
        }
        m_options.append(addOptionRow(choices, group, provider.id, provider.label,
                                      provider.id == QStringLiteral("local")
                                          ? setupText(SetupText::LocalSpeechNote)
                                          : QString(),
                                      QStringLiteral("speechProvider")));
        m_options.last().button->setChecked(provider.id == savedProvider);
    }
    if (!m_options.isEmpty() && selectedIndex() < 0) {
        m_options.first().button->setChecked(true);
    }

    // Almost everyone signs in with the service itself, so that stays the
    // silent default; CLI Proxy API is the explicit exception a checkbox opts
    // into, in a card of its own — inside the service card the control read as
    // a second service picker. Someone whose only login lives in CLI Proxy API
    // opts in here instead of failing the probe and hunting through Settings.
    QFormLayout *signIn = addCard(layout, this, QStringLiteral("Sign-in"));
    QWidget *host = signIn->parentWidget();
    // settingsCardForm -> card frame -> the titled section, which is what has
    // to disappear for a provider without these controls; hiding only the card
    // would leave the bold header floating.
    m_signInSection = host->parentWidget()->parentWidget();
    m_useCliproxy = new QCheckBox(host);
    m_useCliproxy->setObjectName(QStringLiteral("speechUseCliproxy"));
    m_signInSourceRow = settings::makeRow(
        ProviderSignIn::cliproxyOptInLabel(),
        QString(),
        m_useCliproxy,
        host);
    settings::addCardRow(signIn, m_signInSourceRow, host);
    m_cliproxyAccount = new QComboBox(host);
    m_cliproxyAccount->setObjectName(QStringLiteral("speechCliproxyAccount"));
    m_cliproxyAccountRow = settings::makeRow(
        setupText(SetupText::CliproxyAccount),
        QStringLiteral("Refinement by the same company uses this account too."),
        m_cliproxyAccount,
        host);
    settings::addCardRow(signIn, m_cliproxyAccountRow, host);
    m_cliproxyDir = new QLineEdit(host);
    m_cliproxyDir->setObjectName(QStringLiteral("speechCliproxyDir"));
    m_cliproxyDir->setClearButtonEnabled(true);
    const SettingsRow &directoryRow = setupSchemaRow(QStringLiteral("cliproxyOauthDir"));
    m_cliproxyDirRow = settings::makeRow(directoryRow.label, directoryRow.help, m_cliproxyDir, host);
    settings::addCardRow(signIn, m_cliproxyDirRow, host);

    m_hint->setObjectName(QStringLiteral("speechProviderHint"));
    m_hint->setWordWrap(true);
    m_status->setObjectName(QStringLiteral("speechProviderStatus"));
    m_status->setWordWrap(true);
    m_checkAgain->setObjectName(QStringLiteral("speechProviderCheckAgain"));
    m_checkAgain->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_accuracyPass->setObjectName(QStringLiteral("codexFinalRetranscribe"));
    m_accuracyPass->setChecked(m_settings.codexFinalRetranscribe());
    if (m_local) {
        layout->addWidget(makeLocalSection());
    }
    layout->addWidget(m_stats);
    // Under the facts about the chosen service, where the refinement page puts
    // Fast mode: it is a setting for that service, not part of the sign-in
    // verdict below it. Codex only; see selectProvider().
    const SettingsRow &accuracy = setupSchemaRow(QStringLiteral("codexFinalRetranscribe"));
    m_accuracyRow = settings::makeRow(accuracy.label, accuracy.help, m_accuracyPass, this);
    layout->addWidget(m_accuracyRow);
    // One status line, with a warning sign while the service cannot be used;
    // the hint under it is the next step.
    auto *statusLine = new QHBoxLayout;
    statusLine->setSpacing(settings::relatedSpacing());
    m_statusGlyph = makeGlyph(this, QStringLiteral("dialog-warning"), QStyle::SP_MessageBoxWarning);
    m_statusGlyph->setObjectName(QStringLiteral("speechProviderStatusGlyph"));
    m_statusGlyph->hide();
    statusLine->addWidget(m_statusGlyph, 0, Qt::AlignTop);
    statusLine->addWidget(m_status, 1);
    layout->addLayout(statusLine);
    layout->addWidget(m_hint);
    layout->addWidget(m_checkAgain, 0, Qt::AlignLeft);
    layout->addStretch();

    for (const ProviderOptionRow &option : m_options) {
        const QString providerId = option.id;
        connect(option.button, &QRadioButton::clicked, this, [this] { m_userSelected = true; });
        connect(option.button, &QRadioButton::toggled, this, [this, providerId](bool checked) {
            if (checked) {
                m_settings.setSpeechProvider(providerId);
                selectProvider(providerId);
            }
        });
    }
    connect(m_checkAgain, &QPushButton::clicked,
            this, &SpeechProviderSetupPage::checkProviders);
    connect(m_accuracyPass, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.setCodexFinalRetranscribe(checked);
    });
    // toggled rather than clicked: the row caption toggles the box through
    // QCheckBox::toggle(), which never emits clicked. This page's own updates
    // stay silent because updateSignInControls() blocks signals around
    // setChecked(); the account combo keeps activated for the same reason.
    connect(m_useCliproxy, &QCheckBox::toggled, this, [this](bool checked) {
        const int index = selectedIndex();
        if (index < 0) {
            return;
        }
        m_signIn.setUseCliproxy(m_options.at(index).id, checked);
        updateSignInControls();
        reprobeSelectedProvider();
    });
    connect(m_cliproxyAccount, &QComboBox::activated, this, [this] {
        const int index = selectedIndex();
        if (index < 0) {
            return;
        }
        m_signIn.setCliproxyAccount(m_options.at(index).id,
                                    m_cliproxyAccount->currentData().toString());
        reprobeSelectedProvider();
    });
    connect(m_cliproxyDir, &QLineEdit::editingFinished, this, [this] {
        const QString directory = m_cliproxyDir->text().trimmed();
        if (directory == m_signIn.configuredAccountDirectory()) {
            return;
        }
        m_signIn.setAccountDirectory(directory);
        updateSignInControls();
        reprobeSelectedProvider();
    });
    selectProvider(m_options.isEmpty() ? QString()
                                       : m_options.at(std::max(0, selectedIndex())).id);
}

QWidget *SpeechProviderSetupPage::makeLocalSection()
{
    m_localSection = new QWidget(this);
    auto *layout = new QVBoxLayout(m_localSection);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(settings::relatedSpacing());
    layout->addWidget(makeGlyphLine(m_localSection, QStringLiteral("computer"), &m_localHardware));
    m_localHardware->setObjectName(QStringLiteral("speechLocalHardware"));

    QFormLayout *card = addCard(layout, m_localSection, QString());
    QWidget *host = card->parentWidget();
    m_localCard = new QWidget(host);
    auto *cardLayout = new QHBoxLayout(m_localCard);
    cardLayout->setContentsMargins(settings::rowPadding());
    cardLayout->setSpacing(settings::largeSpacing());
    auto *text = new QVBoxLayout;
    text->setSpacing(settings::smallSpacing());
    m_localCaption = makeNote(QString(), m_localCard);
    text->addWidget(m_localCaption);
    auto *title = new QHBoxLayout;
    title->setSpacing(settings::smallSpacing() * 2);
    m_localName = new QLabel(m_localCard);
    m_localName->setObjectName(QStringLiteral("speechLocalModelName"));
    QFont bold = m_localName->font();
    bold.setBold(true);
    m_localName->setFont(bold);
    title->addWidget(m_localName);
    m_localRating = new Badge(QString(), Badge::Tone::Neutral, m_localCard);
    m_localRating->setObjectName(QStringLiteral("speechLocalModelRating"));
    title->addWidget(m_localRating, 0, Qt::AlignVCenter);
    title->addStretch();
    text->addLayout(title);
    m_localFacts = new WrappingLabel(m_localCard);
    m_localFacts->setWordWrap(true);
    text->addWidget(m_localFacts);
    cardLayout->addLayout(text, 1);

    auto *action = new QVBoxLayout;
    action->setSpacing(settings::smallSpacing());
    m_localDownload = new QPushButton(m_localCard);
    m_localDownload->setObjectName(QStringLiteral("speechLocalDownload"));
    m_localDownload->setDefault(true);
    action->addWidget(m_localDownload, 0, Qt::AlignRight);
    m_localProgress = new QProgressBar(m_localCard);
    m_localProgress->setTextVisible(false);
    m_localProgress->setRange(0, 1000);
    m_localProgress->setMaximumWidth(settings::gridUnit() * 9);
    action->addWidget(m_localProgress, 0, Qt::AlignRight);
    m_localState = new QLabel(m_localCard);
    m_localState->setObjectName(QStringLiteral("speechLocalState"));
    action->addWidget(m_localState, 0, Qt::AlignRight);
    m_localCancel = new QPushButton(QStringLiteral("Cancel"), m_localCard);
    action->addWidget(m_localCancel, 0, Qt::AlignRight);
    cardLayout->addLayout(action);
    settings::addCardRow(card, m_localCard, host);

    m_compareToggle = new QToolButton(m_localSection);
    m_compareToggle->setObjectName(QStringLiteral("speechLocalCompare"));
    m_compareToggle->setAutoRaise(true);
    m_compareToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_compareToggle->setArrowType(Qt::RightArrow);
    layout->addWidget(m_compareToggle, 0, Qt::AlignLeft);

    const QStringList headers = compareTableHeaders();
    m_compare = new QTableWidget(int(localModelCatalog().size()), int(headers.size()), m_localSection);
    m_compare->setObjectName(QStringLiteral("speechLocalCompareTable"));
    m_compare->setHorizontalHeaderLabels(headers);
    m_compare->verticalHeader()->hide();
    m_compare->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_compare->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_compare->setSelectionMode(QAbstractItemView::SingleSelection);
    m_compare->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_compare->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_compare->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    m_compare->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_compare->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_compare->setItemDelegateForColumn(0, new BadgeDelegate(m_compare));
    m_compare->hide();
    layout->addWidget(m_compare);
    auto *compareNote = makeNote(localModelText(LocalModelText::CompareNote), m_localSection);
    compareNote->setObjectName(QStringLiteral("speechLocalCompareNote"));
    compareNote->hide();
    layout->addWidget(compareNote);

    connect(m_compareToggle, &QToolButton::clicked, this, [this, compareNote] {
        const bool open = !m_compare->isVisible();
        m_compareToggle->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
        m_compare->setVisible(open);
        compareNote->setVisible(open);
        showLocalChoice();
    });
    connect(m_compare, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (row >= 0) setLocalChoice(localModelCatalog().at(row).id);
    });
    connect(m_localDownload, &QPushButton::clicked, this, [this] {
        m_local->download(localChoice());
        setLocalChoice(localChoice().id);
    });
    connect(m_localCancel, &QPushButton::clicked, this, [this] { m_local->cancelDownload(localChoice().id); });
    connect(m_local, &LocalSetup::changed, this, [this] {
        showLocalChoice();
        showSelectedProvider();
    });
    connect(&m_local->models(), &LocalModelStore::downloadProgress, this, [this] { showLocalChoice(); });
    return m_localSection;
}

bool SpeechProviderSetupPage::localSelected() const
{
    const int index = selectedIndex();
    return m_local && index >= 0 && m_options.at(index).id == QStringLiteral("local");
}

const LocalModel &SpeechProviderSetupPage::localChoice() const
{
    return m_local->speechModelChoice();
}

void SpeechProviderSetupPage::setLocalChoice(const QString &modelId)
{
    m_local->chooseSpeechModel(modelId);
    showLocalChoice();
    showSelectedProvider();
}

bool SpeechProviderSetupPage::localDownloadStarted() const
{
    const LocalModel &model = localChoice();
    const auto state = m_local->modelState(model);
    return state.downloaded || state.downloading;
}

QString SpeechProviderSetupPage::localModelId() const
{
    return localSelected() ? localChoice().id : QString();
}

void SpeechProviderSetupPage::showLocalChoice()
{
    if (!m_localSection) {
        return;
    }
    m_localSection->setVisible(localSelected());
    const LocalModel &model = localChoice();
    const auto state = m_local->modelState(model);
    m_localHardware->setText(m_local->hardwareLine());
    m_localCaption->setText(localModelText(state.suggested ? LocalModelText::Suggested : LocalModelText::YourChoice));
    m_localName->setText(model.name);
    m_localRating->setBadge(modelRatingLabel(model.rating), modelRatingTone(model.rating));
    m_localFacts->setText(state.cardFacts);

    const auto progress = m_local->downloadProgress(model.id);
    const bool downloaded = state.downloaded;
    m_localDownload->setVisible(!progress && !downloaded);
    m_localDownload->setEnabled(!state.tooLarge);
    m_localDownload->setText(state.tooLarge ? localModelText(LocalModelText::TooLarge)
                                            : downloadCaption(model.sizeBytes));
    m_localProgress->setVisible(bool(progress));
    m_localCancel->setVisible(bool(progress));
    if (progress) {
        m_localProgress->setValue(progress->second > 0 ? int(progress->first * 1000 / progress->second) : 0);
        m_localState->setText(QStringLiteral("%1 of %2").arg(downloadSizeText(progress->first),
                                                             downloadSizeText(model.sizeBytes)));
    } else if (downloaded) {
        setStatusColor(m_localState, true);
        m_localState->setText(QStringLiteral("Downloaded"));
    } else {
        m_localState->setText(state.problem);
    }
    m_localState->setVisible(!m_localState->text().isEmpty());

    m_compareToggle->setText(m_compare->isVisible()
                                 ? localModelText(LocalModelText::HideOtherModels)
                                 : compareModelsCaption(int(localModelCatalog().size()) - 1));
    for (int row = 0; row < localModelCatalog().size(); ++row) {
        const LocalModel &entry = localModelCatalog().at(row);
        const QStringList cells = m_local->modelState(entry).tableCells;
        for (int column = 0; column < cells.size(); ++column) {
            QTableWidgetItem *item = m_compare->item(row, column);
            if (!item) {
                item = new QTableWidgetItem;
                m_compare->setItem(row, column, item);
            }
            item->setText(cells.at(column));
        }
        QTableWidgetItem *name = m_compare->item(row, 0);
        name->setData(BadgeDelegate::TextRole, modelRatingLabel(entry.rating));
        name->setData(BadgeDelegate::ToneRole, int(modelRatingTone(entry.rating)));
        name->setData(Qt::AccessibleTextRole,
                      QStringLiteral("%1, %2").arg(cells.first(), modelRatingLabel(entry.rating)));
    }
    {
        const QSignalBlocker blocker(m_compare);
        m_compare->selectRow(int(std::find_if(localModelCatalog().cbegin(), localModelCatalog().cend(),
                                              [&model](const LocalModel &entry) { return entry.id == model.id; })
                                 - localModelCatalog().cbegin()));
    }
}

void SpeechProviderSetupPage::chooseProvider(const QString &providerId)
{
    for (const ProviderOptionRow &option : m_options) {
        if (option.id == providerId) {
            m_userSelected = true;
            option.button->setChecked(true);
            return;
        }
    }
}

void SpeechProviderSetupPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_local) {
        m_local->probeHardware();
    }
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
    if (const QString note = deadEnd(); !note.isEmpty()) {
        return note;
    }
    const int index = selectedIndex();
    return setupTranscriptionBlocked(localSelected(), index < 0 ? QString() : m_options.at(index).label);
}

QString SpeechProviderSetupPage::readySummary() const
{
    const int index = selectedIndex();
    if (index < 0) {
        return QString();
    }
    const ProviderOptionRow &option = m_options.at(index);
    if (localSelected()) {
        return setupChecklistLine(QStringLiteral("transcription"),
                                  setupLocalSpeechChoice(localChoice().name));
    }
    return setupChecklistLine(QStringLiteral("transcription"),
                              m_signIn.usingCliproxy(option.id)
                                  ? setupCliproxySpeechChoice(option.label)
                                  : option.label);
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
    const QList<ProviderDescriptor> providers = m_providers.speechProviders();
    const auto it = std::find_if(providers.cbegin(), providers.cend(),
                                 [&providerId](const ProviderDescriptor &provider) {
                                     return provider.id == providerId;
                                 });
    m_hint->setText(it == providers.cend() ? QString() : it->setupHint);
    // The Local card explains itself; the generic facts would repeat it.
    m_stats->setStats(it == providers.cend() || localSelected() ? QVector<ProviderStat>{} : it->stats);
    m_accuracyRow->setVisible(providerId == QStringLiteral("codex"));
    updateSignInControls();
    showLocalChoice();
    showSelectedProvider();
}

void SpeechProviderSetupPage::updateSignInControls()
{
    const int index = selectedIndex();
    const QString providerId = index < 0 ? QString() : m_options.at(index).id;
    const bool known = ProviderSignIn::supportsCliproxy(providerId);
    m_signInSection->setVisible(known);
    if (!known) {
        return;
    }
    const bool cliproxy = m_signIn.usingCliproxy(providerId);
    {
        const QSignalBlocker blocker(m_useCliproxy);
        m_useCliproxy->setChecked(cliproxy);
    }
    setCardRowVisible(m_cliproxyAccountRow, cliproxy);
    setCardRowVisible(m_cliproxyDirRow, cliproxy);
    if (!cliproxy) {
        return;
    }
    {
        const QSignalBlocker blocker(m_cliproxyDir);
        m_cliproxyDir->setText(m_signIn.configuredAccountDirectory());
        // The resolved directory only differs from the text while the text is
        // empty, which is exactly when the placeholder shows.
        m_cliproxyDir->setPlaceholderText(m_signIn.resolvedAccountDirectory());
    }
    populateCliproxyAccounts();
}

void SpeechProviderSetupPage::populateCliproxyAccounts()
{
    const int index = selectedIndex();
    if (index < 0) {
        return;
    }
    const QString providerId = m_options.at(index).id;
    settings::populateCliproxyAccounts(m_cliproxyAccount,
                                       m_signIn.resolvedAccountDirectory(),
                                       ProviderSignIn::cliproxyAccountType(providerId),
                                       m_signIn.cliproxyAccount(providerId));
}

void SpeechProviderSetupPage::reprobeSelectedProvider()
{
    const int index = selectedIndex();
    if (index < 0) {
        return;
    }
    // The old verdict was about the old sign-in; show "Checking…" and hold
    // Next until the probe answers for the new one. Only this provider's
    // sign-in changed, so only it is probed — in CLI Proxy API mode a probe
    // can be an OAuth refresh over the network.
    m_options[index].probed = false;
    m_options[index].generation = ++m_checkGeneration;
    probeProvider(index, m_options.at(index).generation);
    showSelectedProvider();
}

void SpeechProviderSetupPage::checkProviders()
{
    const quint64 generation = ++m_checkGeneration;
    m_pendingProbes = m_options.size();
    for (int index = 0; index < m_options.size(); ++index) {
        m_options[index].generation = generation;
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
                    {false, setupTranscriptionBlocked(false, QString())});
        return;
    }

    const SpeechSettings settings = m_settings.snapshot().speech;
    std::optional<SpeechPrepareJob> job = provider->createPrepareJob(settings);
    if (!job || !job->run) {
        finishProbe(index, generation, provider->prepare(settings));
        return;
    }
    auto prepareJob = std::make_shared<SpeechPrepareJob>(std::move(*job));
    runProviderProbe<SpeechPrepareResult>(
        &m_providers,
        this,
        [prepareJob] { return prepareJob->run(); },
        [this, index, generation, prepareJob](const SpeechPrepareResult &result) {
            if (generation != m_options.at(index).generation) {
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
    // Superseded per row, not per page: a single-provider re-probe must not
    // discard the other rows' in-flight verdicts.
    if (generation != m_options.at(index).generation) {
        return;
    }
    ProviderOptionRow &option = m_options[index];
    option.probed = true;
    option.ok = result.ok;
    option.message = result.message;
    // The Local row's status is its download, which showLocalRowStatus keeps.
    if (option.id != QStringLiteral("local") || !m_local) {
        setStatusColor(option.status, result.ok);
        option.status->setText(setupProviderVerdict(option.id, result.ok));
    }
    showSelectedProvider();
    // A single-provider re-probe runs outside the counted rounds; it must not
    // drive the one-time auto-selection or push the count negative.
    if (m_pendingProbes > 0 && --m_pendingProbes == 0) {
        autoSelectReadyProvider();
    }
}

QString SpeechProviderSetupPage::deadEnd() const
{
    QStringList signIns;
    bool signInFound = false;
    bool endpointSaved = false;
    for (const ProviderOptionRow &option : m_options) {
        // Endpoint is only on the page when the person saved one.
        endpointSaved = endpointSaved || option.id == QStringLiteral("endpoint");
        if (!isSetupSignInProvider(option.id)) {
            continue;
        }
        signIns.append(option.id);
        // A check that has not answered is not a missing sign-in yet.
        signInFound = signInFound || option.ok || !option.probed;
    }
    // canRunAnyModel stays optimistic until the hardware probe answers.
    return setupTranscriptionDeadEnd(signInFound || m_signIn.anyUsableAccount(signIns),
                                     m_local && m_local->canRunAnyModel(), endpointSaved,
                                     !signIns.isEmpty());
}

void SpeechProviderSetupPage::showSelectedProvider()
{
    showProviderStatus();
    const QString note = deadEnd();
    m_deadEnd->setText(note);
    m_deadEnd->setVisible(!note.isEmpty());
    if (note.isEmpty()) {
        return;
    }
    // The note is the verdict; a status line under it would report the same
    // missing sign-in a second time.
    m_statusGlyph->hide();
    m_status->hide();
    setReady(false);
}

void SpeechProviderSetupPage::showProviderStatus()
{
    const int index = selectedIndex();
    if (index < 0) {
        m_status->show();
        setStatusColor(m_status, false);
        m_status->setText(setupTranscriptionBlocked(false, QString()));
        m_status->setToolTip(QString());
        m_statusGlyph->show();
        m_hint->hide();
        m_checkAgain->hide();
        setReady(false);
        return;
    }
    const ProviderOptionRow &option = m_options.at(index);
    if (localSelected()) {
        // Next opens as soon as a download has started: it keeps going while
        // setup continues, and the Ready page shows where it got to.
        const bool downloaded = m_local->modelState(localChoice()).downloaded;
        const bool started = localDownloadStarted();
        setStatusColor(m_status, false);
        m_status->setToolTip(QString());
        m_statusGlyph->hide();
        m_status->setText(downloaded ? QString()
                          : started  ? setupText(SetupText::DownloadContinues)
                                     : setupText(SetupText::DownloadToContinue));
        m_status->setVisible(!m_status->text().isEmpty());
        m_hint->hide();
        m_checkAgain->hide();
        option.status->setText(downloaded ? QStringLiteral("Ready")
                               : started  ? QStringLiteral("Downloading")
                                          : QString());
        setStatusColor(option.status, downloaded);
        setReady(started);
        return;
    }
    m_status->show();
    if (!option.probed) {
        setStatusColor(m_status, false);
        m_status->setToolTip(QString());
        m_statusGlyph->hide();
        m_status->setText(QStringLiteral("Checking…"));
        m_hint->show();
        m_checkAgain->show();
        setReady(false);
        return;
    }
    setStatusColor(m_status, option.ok);
    // A sign-in's own message names files and commands; the hint below
    // already says what to do, so that message is there on hover only. Any
    // other failure, such as a Custom Endpoint without a URL, is the reason.
    const bool showsReason = !isSetupSignInProvider(option.id) && !option.message.isEmpty();
    m_status->setText(option.ok      ? setupProviderReady(option.label)
                      : showsReason ? option.message
                                    : setupTranscriptionBlocked(false, option.label));
    m_status->setToolTip(option.ok || showsReason ? QString() : option.message);
    m_statusGlyph->setVisible(!option.ok);
    m_hint->setVisible(!option.ok);
    m_checkAgain->setVisible(!option.ok);
    setReady(option.ok);
}

void SpeechProviderSetupPage::autoSelectReadyProvider()
{
    if (m_autoSelectDone || m_userSelected) return;
    m_autoSelectDone = true;
    const int index = selectedIndex();
    if (index < 0) return;
    QStringList ready;
    QStringList signIns;
    for (const auto &option : m_options) {
        if (option.ok) ready.append(option.id);
        if (isSetupSignInProvider(option.id)) signIns.append(option.id);
    }
    const auto chosen = setupSpeechChoice(m_options.at(index).id, ready,
                                          m_local && m_local->canRunAnyModel(),
                                          m_signIn.anyUsableAccount(signIns), m_userSelected);
    for (const auto &option : m_options) {
        if (option.id == chosen) option.button->setChecked(true);
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
    QVBoxLayout *layout = makePage(this, findSetupStep(QStringLiteral("microphone"))->intro);
    m_device->setMinimumContentsLength(28);
    m_level->setRange(0, 100);
    m_level->setValue(0);
    m_level->setFormat(QStringLiteral("%p%"));
    m_status->setWordWrap(true);

    // A microphone plugged in while this page is open shows up here.
    auto *checkAgain = new QPushButton(setupText(SetupText::CheckAgain), this);
    checkAgain->setObjectName(QStringLiteral("microphoneCheckAgain"));
    connect(checkAgain, &QPushButton::clicked, this, [this] {
        refreshDevices();
        if (m_active) {
            startMeter();
        }
    });

    auto *form = new QGridLayout;
    form->addWidget(new QLabel(setupSchemaRow(QStringLiteral("audioDevice")).label, this), 0, 0);
    form->addWidget(m_device, 0, 1);
    form->addWidget(checkAgain, 0, 2);
    form->addWidget(new QLabel(inputLevelLabel(), this), 1, 0);
    form->addWidget(m_level, 1, 1, 1, 2);
    form->setColumnStretch(1, 1);
    layout->addLayout(form);
    layout->addWidget(m_status);
    layout->addStretch();

    m_noInputTimer->setSingleShot(true);
    m_noInputTimer->setInterval(kSetupSilentMicrophoneMs);
    connect(m_noInputTimer, &QTimer::timeout, this, [this] {
        if (m_inputDetected) {
            return;
        }
        m_status->setText(setupSilentMicrophoneHint());
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
    return setupMicrophoneBlocked(m_device->count() == 0 ? SetupMicrophoneProblem::NoDevice
                                                         : SetupMicrophoneProblem::Silent);
}

QString MicrophoneSetupPage::readySummary() const
{
    const QString device = m_device->currentText();
    return device.isEmpty() ? QString()
                            : setupChecklistLine(QStringLiteral("microphone"), device);
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
            refreshDevices();
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
    // Read afresh each time: a microphone may have been connected since.
    refreshDevices();
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
            m_status->setText(setupText(SetupText::InputDetected));
            setInputDetected(true);
        }
    });

    QString error;
    if (!m_input->start(&error)) {
        m_status->setText(error);
        return;
    }
    m_status->setText(setupText(SetupText::ListeningForInput));
    m_noInputTimer->start();
}

AccessibilitySetupPage::AccessibilitySetupPage(ApplicationController &controller,
                                               QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_status(new QLabel(this))
    , m_enable(new QPushButton(this))
{
    QVBoxLayout *layout = makePage(
        this,
#ifdef Q_OS_WIN
        QStringLiteral("Windows UI Automation lets Speecher identify the target app, read nearby text, and learn corrections. It does not require a permission grant."));
#else
        findSetupStep(QStringLiteral("accessibility"))->intro);
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
    for (const QString &capability : accessibilityCapabilities()) {
        const StatusRow row = makeStatusRow(host, nullptr, capability, false);
        settings::addCardRow(card, row.widget, host);
        m_capabilities.append(row.status);
    }
    layout->addWidget(m_status);
    layout->addWidget(m_enable, 0, Qt::AlignLeft);
    auto *reassurance = new QLabel(accessibilitySetupFootnote(), this);
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
    status = accessibilitySetupStatus(supported, enabled, persistent);
    const QString action = supported ? accessibilityActionCaption(enabled, persistent) : QString();
    m_enable->setText(action);
    m_enable->setVisible(!action.isEmpty());
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
        capability->setText(accessibilityCapabilityStatus(allowed));
    }
}

QString AccessibilitySetupPage::blockedReason() const
{
    return findSetupStep(QStringLiteral("accessibility"))->blocked;
}

TextDeliverySetupPage::TextDeliverySetupPage(SettingsStore &settings, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_status(new WrappingLabel(this))
    , m_setup(new QPushButton(QStringLiteral("Set up virtual keyboard"), this))
    , m_progress(new QProgressBar(this))
    , m_clipboardOnly(new QCheckBox(this))
    , m_restoreClipboard(new QCheckBox(this))
    , m_format(new QComboBox(this))
{
    QVBoxLayout *layout = makePage(this, findSetupStep(QStringLiteral("delivery"))->intro);
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

    m_clipboardOnlyRow = settings::makeRow(QStringLiteral("Copy to the clipboard instead"),
                                           QStringLiteral("Skip the virtual keyboard. Speecher copies "
                                                          "your text and you paste it yourself."),
                                           m_clipboardOnly, host);
    settings::addCardRow(card, m_clipboardOnlyRow, host);
    settings::addCardRow(
        card,
        settings::makeRow(setupSchemaRow(QStringLiteral("outputFormat")).label, QString(), m_format, host),
        host);
    const SettingsRow &restore = setupSchemaRow(QStringLiteral("restoreClipboardAfterTyping"));
    settings::addCardRow(card, settings::makeRow(restore.label, restore.help, m_restoreClipboard, host), host);
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
    return findSetupStep(QStringLiteral("delivery"))->blocked;
}

QString TextDeliverySetupPage::readySummary() const
{
#ifdef SPEECHER_WITH_YDOTOOL
    if (!m_clipboardOnly->isChecked()) {
        return setupChecklistLine(QStringLiteral("delivery"), QStringLiteral("virtual keyboard"));
    }
#endif
    return setupChecklistLine(QStringLiteral("delivery"), QStringLiteral("clipboard"));
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
    // The state in words; which program is missing is detail for the tooltip.
    m_status->setText(needsSignIn
                          ? QStringLiteral("Almost done — log out of your computer and back in, then turn on the virtual keyboard in Settings > Output.")
                          : QStringLiteral("Virtual keyboard: %1").arg(status.label));
    m_status->setToolTip(needsSignIn ? QString() : status.detail);
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
                                         LocalSetup *local,
                                         QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_local(local)
    , m_stats(new ProviderStatsBlock(this))
    , m_warning(new WrappingLabel(this))
    , m_fastMode(new QCheckBox(setupSchemaRow(QStringLiteral("anthropicFastMode")).label, this))
    , m_openAiSpeedRow(new QWidget(this))
    , m_openAiSpeed(new QComboBox(m_openAiSpeedRow))
    , m_fastModeHint(new QLabel(this))
{
    QVBoxLayout *layout = makePage(this, findSetupStep(QStringLiteral("refinement"))->intro);

    // The cloud providers use a sign-in; a local runner and a custom endpoint
    // are models the person runs. Each group is its own card.
    m_group = new QButtonGroup(this);
    const QString savedProvider = m_settings.refinementProvider();
    const QList<ProviderDescriptor> registered = providers.refinementProviders();
    const auto addGroup = [&](const QString &title, const QStringList &ids) {
        QFormLayout *card = nullptr;
        for (const QString &id : ids) {
            const auto found = std::find_if(registered.cbegin(), registered.cend(),
                                            [&id](const ProviderDescriptor &provider) { return provider.id == id; });
            if (found == registered.cend()) {
                continue;
            }
            const ProviderDescriptor &provider = *found;
            if (!card) {
                card = addCard(layout, this, title);
            }
            m_options.append(addOptionRow(card, m_group, provider.id, provider.label, provider.setupHint,
                                          QStringLiteral("refinementProvider")));
            m_options.last().button->setChecked(provider.id == savedProvider);
        }
    };
    addGroup(setupText(SetupText::UsesYourSignIn), {QStringLiteral("anthropic"), QStringLiteral("openai")});
    addGroup(setupText(SetupText::YourOwnModels), {QStringLiteral("local"), QStringLiteral("endpoint")});
    // A registry with providers this page does not group still offers them.
    const QStringList grouped{QStringLiteral("anthropic"), QStringLiteral("openai"), QStringLiteral("local"),
                        QStringLiteral("endpoint")};
    QStringList others;
    for (const ProviderDescriptor &provider : registered) {
        if (!grouped.contains(provider.id)) {
            others.append(provider.id);
        }
    }
    addGroup(setupText(SetupText::CleanupProvider), others);

    m_skip = new QCheckBox(setupText(SetupText::SkipCleanup), this);
    m_skip->setObjectName(QStringLiteral("refinementSkip"));
    m_skip->setChecked(selectedIndex() < 0);
    layout->addWidget(m_skip);
    m_lastProvider = selectedIndex() >= 0 ? selectedProviderId()
                     : m_options.isEmpty() ? QString()
                                           : m_options.first().id;

    m_warning->setObjectName(QStringLiteral("refinementProviderWarning"));
    m_warning->setWordWrap(true);
    m_warning->hide();
    // Directly under the cards, so it reads as attached to the selection above
    // it rather than to the facts below.
    layout->addWidget(m_warning);
    if (m_local) {
        layout->addWidget(makeLocalRunnerDetail());
        layout->addWidget(makeEndpointDetail());
    }
    layout->addWidget(m_stats);
    m_fastMode->setObjectName(QStringLiteral("refinementFastMode"));
    m_openAiSpeed->setObjectName(QStringLiteral("refinementOpenAiSpeed"));
    auto *speedLayout = new QHBoxLayout(m_openAiSpeedRow);
    speedLayout->setContentsMargins(0, 0, 0, 0);
    auto *speedLabel = new QLabel(QStringLiteral("Speed"), m_openAiSpeedRow);
    speedLabel->setBuddy(m_openAiSpeed);
    speedLayout->addWidget(speedLabel);
    speedLayout->addWidget(m_openAiSpeed);
    speedLayout->addStretch();
    m_fastModeHint->setWordWrap(true);
    layout->addWidget(m_fastMode);
    layout->addWidget(m_openAiSpeedRow);
    layout->addWidget(m_fastModeHint);
    layout->addStretch();

    for (const ProviderOptionRow &option : m_options) {
        const QString providerId = option.id;
        connect(option.button, &QRadioButton::clicked, this, [this] { m_userSelected = true; });
        connect(option.button, &QRadioButton::toggled, this, [this, providerId](bool checked) {
            if (checked) {
                m_lastProvider = providerId;
                const QSignalBlocker blocker(m_skip);
                m_skip->setChecked(false);
                selectProvider(providerId);
            }
        });
    }
    connect(m_skip, &QCheckBox::clicked, this, [this](bool skip) {
        m_userSelected = true;
        skipCleanup(skip);
    });
    connect(m_fastMode, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings.setAnthropicFastMode(checked);
    });
    connect(m_openAiSpeed, &QComboBox::currentIndexChanged, this, [this] {
        m_settings.setOpenAiSpeed(m_openAiSpeed->currentData().toString());
    });
    selectProvider(selectedProviderId());
}

// None is not a provider card but a way out of all of them: checking it
// clears the choice, unchecking it returns to the last provider.
void RefinementSetupPage::skipCleanup(bool skip)
{
    if (!skip) {
        for (const ProviderOptionRow &option : m_options) {
            if (option.id == m_lastProvider) {
                option.button->setChecked(true);
                return;
            }
        }
        return;
    }
    m_group->setExclusive(false);
    for (const ProviderOptionRow &option : m_options) {
        option.button->setChecked(false);
    }
    m_group->setExclusive(true);
    selectProvider(QStringLiteral("none"));
}

QWidget *RefinementSetupPage::makeLocalRunnerDetail()
{
    m_localDetail = new QWidget(this);
    auto *layout = new QVBoxLayout(m_localDetail);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(settings::relatedSpacing());
    m_runnerStatus = new WrappingLabel(m_localDetail);
    m_runnerStatus->setObjectName(QStringLiteral("refinementRunnerStatus"));
    m_runnerStatus->setWordWrap(true);
    layout->addWidget(m_runnerStatus);

    QFormLayout *card = addCard(layout, m_localDetail, QString());
    // addCard puts the card in a section of its own as the layout's last item.
    m_runnerCard = layout->itemAt(layout->count() - 1)->widget();
    QWidget *host = card->parentWidget();
    m_runnerModel = new QComboBox(host);
    m_runnerModel->setObjectName(QStringLiteral("refinementRunnerModel"));
    m_runnerModel->setMinimumContentsLength(20);
    const SettingsRow &runnerModel = setupSchemaRow(QStringLiteral("localRunnerModel"));
    m_runnerModelRow = settings::makeRow(runnerModel.label, runnerModel.help, m_runnerModel, host);
    settings::addCardRow(card, m_runnerModelRow, host);

    m_cleanupSuggestion = new QWidget(host);
    auto *suggestion = new QHBoxLayout(m_cleanupSuggestion);
    suggestion->setContentsMargins(settings::rowPadding());
    suggestion->setSpacing(settings::largeSpacing());
    m_cleanupSuggestionText = new WrappingLabel(m_cleanupSuggestion);
    m_cleanupSuggestionText->setWordWrap(true);
    suggestion->addWidget(m_cleanupSuggestionText, 1);
    auto *pullColumn = new QVBoxLayout;
    m_pull = new QPushButton(setupText(SetupText::DownloadWithOllama), m_cleanupSuggestion);
    m_pull->setObjectName(QStringLiteral("refinementPull"));
    pullColumn->addWidget(m_pull, 0, Qt::AlignRight);
    m_pullProgress = new QProgressBar(m_cleanupSuggestion);
    m_pullProgress->setTextVisible(false);
    m_pullProgress->setRange(0, 1000);
    m_pullProgress->setMaximumWidth(settings::gridUnit() * 9);
    pullColumn->addWidget(m_pullProgress, 0, Qt::AlignRight);
    suggestion->addLayout(pullColumn);
    settings::addCardRow(card, m_cleanupSuggestion, host);

    m_noRunner = new QWidget(m_localDetail);
    auto *noRunner = new QVBoxLayout(m_noRunner);
    noRunner->setContentsMargins(0, 0, 0, 0);
    noRunner->setSpacing(settings::relatedSpacing());
    auto *explain = makeNote(setupText(SetupText::InstallRunner), m_noRunner);
    noRunner->addWidget(explain);
    auto *buttons = new QHBoxLayout;
    auto *getOllama = new QPushButton(setupText(SetupText::GetOllama), m_noRunner);
    getOllama->setObjectName(QStringLiteral("refinementGetOllama"));
    auto *checkAgain = new QPushButton(setupText(SetupText::CheckAgain), m_noRunner);
    checkAgain->setObjectName(QStringLiteral("refinementRunnerCheckAgain"));
    buttons->addWidget(getOllama);
    buttons->addWidget(checkAgain);
    buttons->addStretch();
    noRunner->addLayout(buttons);
    auto *rawWarning = new InlineMessage(m_noRunner);
    rawWarning->setType(InlineMessage::Type::Warning);
    rawWarning->setCloseButtonVisible(false);
    rawWarning->setText(setupText(SetupText::RawUntilRunner));
    noRunner->addWidget(rawWarning);
    layout->addWidget(m_noRunner);

    connect(getOllama, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl(QStringLiteral("https://ollama.com/download"))); });
    connect(checkAgain, &QPushButton::clicked, this, [this] { m_local->detectRunners(); });
    connect(m_pull, &QPushButton::clicked, this, [this] {
        if (const std::optional<CleanupModel> model = m_local->suggestedCleanupModel()) {
            m_local->pullCleanupModel(model->ollamaTag);
        }
    });
    connect(m_runnerModel, &QComboBox::activated, this, [this] {
        LocalRunnerSettings runner = m_settings.localRunnerSettings();
        runner.model = m_runnerModel->currentText();
        m_settings.setLocalRunnerSettings(runner);
        showSelectedProvider();
    });
    connect(m_local, &LocalSetup::changed, this, [this] {
        showLocalRunner();
        autoSelectReadyProvider();
    });
    connect(m_local, &LocalSetup::pullProgress, this, [this] { showLocalRunner(); });
    return m_localDetail;
}

QWidget *RefinementSetupPage::makeEndpointDetail()
{
    m_endpointDetail = new QWidget(this);
    auto *layout = new QVBoxLayout(m_endpointDetail);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(settings::relatedSpacing());
    QFormLayout *card = addCard(layout, m_endpointDetail, QString());
    QWidget *host = card->parentWidget();
    // With the CLI Proxy API preset, the server and key shown are the proxy's.
    const RefinementEndpoint saved = resolvedRefinementEndpoint(m_settings.snapshot().refinement);

    m_endpointFormat = new QComboBox(host);
    m_endpointFormat->setObjectName(QStringLiteral("refinementEndpointFormat"));
    addOptions(m_endpointFormat, setupSchemaRow(QStringLiteral("refinementEndpointFormat")).options(AppSettings()));
    settings::selectData(m_endpointFormat, saved.format);
    settings::addCardRow(
        card,
        settings::makeRow(setupSchemaRow(QStringLiteral("refinementEndpointFormat")).label, QString(),
                          m_endpointFormat, host),
        host);

    m_endpointUrl = new QLineEdit(saved.apiBase, host);
    m_endpointUrl->setObjectName(QStringLiteral("refinementEndpointUrl"));
    m_endpointUrl->setPlaceholderText(QStringLiteral("http://localhost:8080/v1"));
    m_endpointUrl->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    settings::addCardRow(card, settings::makeRow(setupSchemaRow(QStringLiteral("refinementEndpointUrl")).label, QString(), m_endpointUrl, host), host);

    m_endpointKey = new QLineEdit(saved.apiKey, host);
    m_endpointKey->setObjectName(QStringLiteral("refinementEndpointKey"));
    m_endpointKey->setEchoMode(QLineEdit::Password);
    m_endpointKey->setPlaceholderText(QStringLiteral("Optional"));
    m_endpointKey->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    settings::addCardRow(card, settings::makeRow(setupSchemaRow(QStringLiteral("refinementEndpointApiKey")).label, keyStorageHelp(),
                                                 m_endpointKey, host),
                         host);

    auto *modelControls = new QWidget(host);
    auto *modelLayout = new QHBoxLayout(modelControls);
    modelLayout->setContentsMargins(0, 0, 0, 0);
    modelLayout->setSpacing(settings::relatedSpacing());
    m_endpointModel = new QComboBox(modelControls);
    m_endpointModel->setObjectName(QStringLiteral("refinementEndpointModel"));
    m_endpointModel->setEditable(true);
    m_endpointModel->setInsertPolicy(QComboBox::NoInsert);
    m_endpointModel->setMinimumContentsLength(18);
    m_endpointModel->lineEdit()->setClearButtonEnabled(true);
    m_endpointModel->setEditText(saved.model);
    modelLayout->addWidget(m_endpointModel);
    auto *connectButton = new QPushButton(QStringLiteral("Connect"), modelControls);
    connectButton->setObjectName(QStringLiteral("refinementEndpointConnect"));
    modelLayout->addWidget(connectButton);
    settings::addCardRow(card, settings::makeRow(setupSchemaRow(QStringLiteral("refinementEndpointModel")).label,
                                                 setupText(SetupText::EndpointModelHint),
                                                 modelControls, host),
                         host);
    m_endpointStatus = new WrappingLabel(m_endpointDetail);
    m_endpointStatus->setObjectName(QStringLiteral("refinementEndpointStatus"));
    m_endpointStatus->setWordWrap(true);
    layout->addWidget(m_endpointStatus);

    // Compare against what this field showed, never against a newly read secret.
    const auto bindText = [this](QLineEdit *field, bool key) {
        connect(field, &QLineEdit::editingFinished, this,
                [this, field, key, shown = field->text()]() mutable {
                    if (field->text() == shown) return;
                    shown = field->text();
                    saveEndpointEdit(key ? RefinementEndpointEdit{.apiKey = shown}
                                         : RefinementEndpointEdit{.baseUrl = shown});
                });
    };
    bindText(m_endpointUrl, false);
    bindText(m_endpointKey, true);
    connect(m_endpointFormat, &QComboBox::activated, this, [this] {
        saveEndpointEdit({.format = m_endpointFormat->currentData().toString()});
    });
    connect(m_endpointModel, &QComboBox::currentTextChanged, this, [this](const QString &model) {
        saveEndpointEdit({.model = model});
    });
    connect(connectButton, &QPushButton::clicked, this, [this] {
        emit m_endpointUrl->editingFinished();
        emit m_endpointKey->editingFinished();
        m_local->checkRefinementEndpoint(m_settings.snapshot().refinement);
    });
    connect(m_local, &LocalSetup::changed, this, [this] { showEndpointCheck(); });
    return m_endpointDetail;
}

void RefinementSetupPage::saveEndpointEdit(const RefinementEndpointEdit &edit)
{
    AppSettings settings = m_settings.snapshot();
    editRefinementEndpoint(settings, edit);
    m_settings.applySnapshot(settings);
    showEndpointCheck();
    showSelectedProvider();
}

void RefinementSetupPage::showEndpointCheck()
{
    const LiveFacts facts = m_local->liveFacts();
    m_endpointStatus->setText(facts.refinementEndpointStatus);
    m_endpointStatus->setVisible(!facts.refinementEndpointStatus.isEmpty());
    QStringList shown;
    for (int i = 0; i < m_endpointModel->count(); ++i) shown.append(m_endpointModel->itemText(i));
    if (shown != facts.refinementEndpointModels) {
        const QString typed = m_endpointModel->currentText();
        const int cursor = m_endpointModel->lineEdit()->cursorPosition();
        const QSignalBlocker blocker(m_endpointModel);
        m_endpointModel->clear();
        m_endpointModel->addItems(facts.refinementEndpointModels);
        m_endpointModel->setEditText(typed);
        m_endpointModel->lineEdit()->setCursorPosition(cursor);
    }
    // A check that finds no model saved picks the server's first.
    const QString saved = m_settings.refinementEndpointSettings().model;
    if (m_endpointModel->currentText().isEmpty() && !saved.isEmpty()) {
        const QSignalBlocker blocker(m_endpointModel);
        m_endpointModel->setCurrentIndex(m_endpointModel->findText(saved));
        m_endpointModel->setEditText(saved);
    }
}

void RefinementSetupPage::showLocalRunner()
{
    const auto choice = m_local->runnerChoice();
    const LocalSetup::Pull pull = m_local->pull();
    const bool found = choice.available.has_value();
    // The own-model rows say what is on this computer, not a sign-in verdict.
    for (const ProviderOptionRow &option : m_options) {
        if (option.id == QStringLiteral("local")) {
            setStatusColor(option.status, found);
            option.status->setText(m_local->detectingRunners() ? QStringLiteral("Checking…")
                                   : found ? QStringLiteral("%1 found").arg(choice.available->name)
                                           : setupText(SetupText::NoRunner));
        } else if (option.id == QStringLiteral("endpoint")) {
            option.status->clear();
        }
    }
    if (m_local->detectingRunners()) {
        setStatusColor(m_runnerStatus, false);
        m_runnerStatus->setText(setupText(SetupText::LookingForRunners));
    } else if (found) {
        const DetectedRunner &runner = *choice.available;
        setStatusColor(m_runnerStatus, true);
        m_runnerStatus->setText(QStringLiteral("%1 %2 is running on this computer.").arg(runner.name, runner.version));
    } else {
        setStatusColor(m_runnerStatus, false);
        m_runnerStatus->setText(choice.selection.runner.isEmpty()
            ? setupText(SetupText::NoRunnerFound)
            : QStringLiteral("%1 is unavailable. Your saved selection is unchanged.").arg(localRunnerName(choice.selection.runner)));
    }
    m_noRunner->setVisible(!found && !m_local->detectingRunners());
    m_runnerCard->setVisible(found);

    {
        const QSignalBlocker blocker(m_runnerModel);
        m_runnerModel->clear();
        if (found) m_runnerModel->addItems(choice.available->models);
        if (!choice.selection.model.isEmpty() && m_runnerModel->findText(choice.selection.model) < 0)
            m_runnerModel->addItem(choice.selection.model);
        m_runnerModel->setCurrentIndex(m_runnerModel->findText(choice.selection.model));
        m_runnerModelRow->setVisible(found);
    }
    const std::optional<CleanupModel> suggested = m_local->suggestedCleanupModel();
    settings::setCardRowVisible(m_cleanupSuggestion, choice.showSuggestion || pull.running);
    m_pull->setVisible(!pull.running && choice.offerPull);
    m_pullProgress->setVisible(pull.running);
    if (pull.running) {
        m_pullProgress->setValue(pull.totalBytes > 0 ? int(pull.completedBytes * 1000 / pull.totalBytes) : 0);
        m_cleanupSuggestionText->setText(QStringLiteral("Downloading %1 through Ollama: %2 of %3")
                                             .arg(suggested ? suggested->name : pull.tag,
                                                  downloadSizeText(pull.completedBytes),
                                                  downloadSizeText(pull.totalBytes)));
    } else if (suggested) {
        m_cleanupSuggestionText->setText(
            QStringLiteral("Suggested for this computer: %1, %2.%3")
                .arg(suggested->name, downloadSizeText(suggested->sizeBytes),
                     pull.error.isEmpty() ? QString() : QStringLiteral("\n") + pull.error));
    } else {
        m_cleanupSuggestionText->setText(QStringLiteral("Cleanup would be slow on this computer. A cloud "
                                                        "provider or skipping cleanup will feel faster."));
    }
}

void RefinementSetupPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Same reasoning as the transcription page: probe when the page is shown,
    // not while the wizard is building, so the credential reads serialize and
    // a mid-wizard sign-in is picked up. Runners first, so a probe that
    // answers at once finds the runner check still pending.
    if (m_local) {
        m_local->probeHardware();
        m_local->detectRunners();
    }
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
    const QString ownModel = ownModelRefinementSummary(m_settings.snapshot().refinement);
    if (!ownModel.isEmpty()) {
        return setupChecklistLine(QStringLiteral("refinement"), ownModel);
    }
    const int index = selectedIndex();
    return setupChecklistLine(QStringLiteral("refinement"),
                              index < 0 ? QStringLiteral("None") : m_options.at(index).label);
}

// The row's verdict as Settings would find it now: an own model by its saved
// form, a sign-in by the page's last probe.
std::optional<bool> RefinementSetupPage::providerReady() const
{
    const QString id = selectedProviderId();
    if (id == QStringLiteral("local") || id == QStringLiteral("endpoint")) {
        const RefinementSettings settings = m_settings.snapshot().refinement;
        TranscriptRefiner *refiner = m_providers.refinementProvider(id);
        return refiner && refiner->prepare(settings).ok;
    }
    const int index = selectedIndex();
    if (index < 0 || !m_options.at(index).probed) {
        return std::nullopt;
    }
    return m_options.at(index).ok;
}

QString RefinementSetupPage::readyVerdict() const
{
    return setupRefinementStatus(selectedProviderId(), providerReady());
}

bool RefinementSetupPage::readyVerdictPositive() const
{
    return providerReady().value_or(false);
}

QString RefinementSetupPage::selectedProviderId() const
{
    const int index = selectedIndex();
    return index < 0 ? QStringLiteral("none") : m_options.at(index).id;
}

void RefinementSetupPage::selectProvider(const QString &providerId)
{
    m_settings.setRefinementProvider(providerId);
    if (m_localDetail) {
        m_localDetail->setVisible(providerId == QStringLiteral("local"));
        m_endpointDetail->setVisible(providerId == QStringLiteral("endpoint"));
        showLocalRunner();
        showEndpointCheck();
    }
    updateProviderStats();
    updateFastModeControl();
    showSelectedProvider();
}

void RefinementSetupPage::checkProviders()
{
    const quint64 generation = ++m_checkGeneration;
    m_pendingProbes = m_options.size();
    for (int index = 0; index < m_options.size(); ++index) {
        m_options[index].generation = generation;
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
    runProviderProbe<RefinementRefreshResult>(
        &m_providers,
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
    option.status->setText(setupProviderVerdict(option.id, result.ok));
    if (m_local && (option.id == QStringLiteral("local") || option.id == QStringLiteral("endpoint"))) {
        showLocalRunner();
    }
    showSelectedProvider();
    // Unlike the speech page, this page has no single-provider re-probe:
    // every probe belongs to a counted round, and the guard only keeps a
    // stray result from pushing the count negative.
    if (m_pendingProbes > 0 && --m_pendingProbes == 0) {
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
    // than a gate: dictation still delivers, just without the cleanup. The
    // own-model choices show their own state instead.
    const bool ownModel = m_localDetail && (option.id == QStringLiteral("local")
                                            || option.id == QStringLiteral("endpoint"));
    const bool warn = option.probed && !option.ok && !ownModel;
    m_warning->setVisible(warn);
    if (warn) {
        setStatusColor(m_warning, false);
        m_warning->setText(
            setupRefinementNotSignedIn(option.label));
    }
}

void RefinementSetupPage::autoSelectReadyProvider()
{
    const bool probing = std::any_of(m_options.cbegin(), m_options.cend(), [](const auto &option) { return !option.probed; });
    if (m_autoSelectDone || m_userSelected || probing || (m_local && m_local->detectingRunners())) return;
    m_autoSelectDone = true;
    const int index = selectedIndex();
    if (index < 0) return;
    QStringList ready;
    for (const auto &option : m_options) if (option.ok) ready.append(option.id);
    const bool runnerFound = m_local && m_local->runnerChoice().available;
    const auto chosen = setupRefinementChoice(m_options.at(index).id, ready, runnerFound,
                                              m_settings.refinementProviderChosen());
    if (chosen == QStringLiteral("none")) {
        m_skip->setChecked(true);
        skipCleanup(true);
        return;
    }
    for (const auto &option : m_options) {
        if (option.id == chosen) option.button->setChecked(true);
    }
}

void RefinementSetupPage::updateProviderStats()
{
    const QString providerId = selectedProviderId();
    if (m_localDetail && (providerId == QStringLiteral("local") || providerId == QStringLiteral("endpoint"))) {
        m_stats->setStats({});
        return;
    }
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
    m_fastMode->setVisible(anthropic);
    m_openAiSpeedRow->setVisible(openAi);
    m_fastModeHint->setVisible(openAi || anthropic);
    if (openAi) {
        m_fastModeHint->setText(openAiSpeedHelp());
        m_openAiSpeed->setToolTip(fastModeTooltip(provider));
        const QString model = m_settings.openAiModel();
        const QSignalBlocker blocker(m_openAiSpeed);
        m_openAiSpeed->clear();
        for (const RowOption &option : openAiSpeedOptions(model)) {
            m_openAiSpeed->addItem(option.label, option.id);
            settings::setComboItemEnabled(m_openAiSpeed, m_openAiSpeed->count() - 1, option.enabled,
                                          option.enabled ? QString() : option.help);
        }
        settings::selectData(m_openAiSpeed, m_settings.openAiSpeed());
    } else if (anthropic) {
        m_fastModeHint->setText(fastModeHelp(provider));
        m_fastMode->setToolTip(fastModeTooltip(provider));
        const QSignalBlocker blocker(m_fastMode);
        m_fastMode->setChecked(m_settings.anthropicFastMode());
    }
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
    QVBoxLayout *layout = makePage(this, setupReadyIntro(false, false), &m_intro);
    setStatusColor(m_intro, true);
    m_downloadNotice = new InlineMessage(this);
    m_downloadNotice->setObjectName(QStringLiteral("finishDownloadNotice"));
    m_downloadNotice->setCloseButtonVisible(false);
    m_downloadNotice->setText(setupText(SetupText::CloseWhileDownloading));
    m_downloadNotice->hide();
    layout->addWidget(m_downloadNotice);
    // The download finishing changes the intro and the step's row, and a
    // cancelled one sends the person back to choose again.
    connect(m_controller.localSetup(), &LocalSetup::changed, this, [this] { setSteps(m_steps); });
    connect(m_controller.localModelStore(), &LocalModelStore::downloadProgress,
            this, &FinishSetupPage::showDownloadProgress);
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
                                 setupText(SetupText::HowToDictate));
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
    blockedLayout->addWidget(settings::makeSectionLabel(setupBlockedHeading(), m_blocked));
    m_blockedList = new QWidget(m_blocked);
    auto *blockedListLayout = new QVBoxLayout(m_blockedList);
    blockedListLayout->setContentsMargins(0, 0, 0, 0);
    blockedListLayout->setSpacing(0);
    blockedLayout->addWidget(m_blockedList);
    auto *blockedFoot = new WrappingLabel(setupBlockedFooter(), m_blocked);
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
    m_steps = steps;
    const bool blocked = std::any_of(steps.cbegin(), steps.cend(),
                                     [](const SetupStepStatus &step) { return !step.ok; });
    const bool downloading = !blocked && !downloadingModel().isEmpty();
    m_intro->setText(setupReadyIntro(blocked, downloading));
    setStatusColor(m_intro, !blocked && !downloading);
    m_downloadNotice->setVisible(downloading);
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
        auto *goToStep = new QPushButton(setupText(SetupText::GoToStep), host);
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
    m_downloadProgress = nullptr;
    m_downloadText = nullptr;
    auto *layout = qobject_cast<QVBoxLayout *>(m_completedList->layout());
    QFormLayout *card = nullptr;
    const QString downloading = downloadingModel();
    for (const SetupStepStatus &step : steps) {
        // Only the steps where something was chosen have anything to report.
        if (step.detail.isEmpty()) {
            continue;
        }
        if (!card) {
            card = addCard(layout, m_completedList, QString());
        }
        QWidget *host = card->parentWidget();
        QWidget *download = nullptr;
        if (!downloading.isEmpty() && step.localModelId == downloading) {
            // The download this step chose is still going: its row carries
            // the progress instead of a verdict.
            download = new QWidget(host);
            auto *downloadLayout = new QHBoxLayout(download);
            downloadLayout->setContentsMargins(0, 0, 0, 0);
            downloadLayout->setSpacing(settings::relatedSpacing());
            m_downloadProgress = new QProgressBar(download);
            m_downloadProgress->setObjectName(QStringLiteral("finishDownloadProgress"));
            m_downloadProgress->setTextVisible(false);
            m_downloadProgress->setRange(0, 1000);
            m_downloadProgress->setFixedWidth(settings::gridUnit() * 7);
            downloadLayout->addWidget(m_downloadProgress);
            m_downloadText = new QLabel(download);
            m_downloadText->setForegroundRole(QPalette::PlaceholderText);
            downloadLayout->addWidget(m_downloadText);
            auto *cancel = new QPushButton(QStringLiteral("Cancel"), download);
            cancel->setObjectName(QStringLiteral("finishDownloadCancel"));
            connect(cancel, &QPushButton::clicked, this,
                    [this, downloading] { m_controller.localSetup()->cancelDownload(downloading); });
            downloadLayout->addWidget(cancel);
        }
        const StatusRow row = makeStatusRow(host, nullptr, step.detail, false, download);
        if (!download) {
            setStatusColor(row.status, step.verdictReady);
            row.status->setText(step.verdict.isEmpty() ? QStringLiteral("Ready") : step.verdict);
        }
        settings::addCardRow(card, row.widget, host);
    }
    showRebuiltList(m_completedList);
    showDownloadProgress();
}

QString FinishSetupPage::downloadingModel() const
{
    for (const SetupStepStatus &step : m_steps) {
        if (!step.localModelId.isEmpty() && m_controller.localModelStore()->isDownloading(step.localModelId)) {
            return step.localModelId;
        }
    }
    return {};
}

void FinishSetupPage::showDownloadProgress()
{
    const QString modelId = downloadingModel();
    if (!m_downloadProgress || modelId.isEmpty()) {
        return;
    }
    const auto progress = m_controller.localSetup()->downloadProgress(modelId);
    if (!progress) {
        return;
    }
    m_downloadProgress->setValue(progress->second > 0 ? int(progress->first * 1000 / progress->second) : 0);
    m_downloadText->setText(QStringLiteral("%1 of %2").arg(downloadSizeText(progress->first),
                                                           downloadSizeText(progress->second)));
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
            setupActivationInstruction(m_controller.settings()->shortcutActivationMode(), display));
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
