#include "ui/SettingsDialog.h"

#include "app/ApplicationController.h"
#include "core/OutputMethod.h"
#include "core/SecretStore.h"
#include "core/SettingsStore.h"
#include "core/VocabularyLimit.h"
#include "output/YdotoolDelivery.h"
#include "output/YdotoolSetup.h"
#include "providers/OpenAiAuthProvider.h"
#include "providers/ProviderRegistry.h"
#include "ui/Theme.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>

namespace speecher {

static const char *settingsStyle =
    "QFrame#settingsCard,QFrame#vocabSection{background:palette(base);border:1px solid palette(mid);border-radius:12px;}"
    "QFrame#settingsRow{background:transparent;border:0;}"
    "QWidget#rowText{background:transparent;border:0;}"
    "QTableWidget#vocabInput{background:palette(base);color:palette(text);border:1px solid palette(mid);border-radius:6px;gridline-color:palette(mid);}"
    "QTableWidget#vocabInput:focus{border-color:palette(highlight);}"
    "QFrame#settingsCard QLabel,QFrame#vocabSection QLabel,QLabel#noteText,QLabel#sectionLabel{background:transparent;border:0;}"
    "QFrame#settingsSeparator{background:palette(mid);border:0;margin:0;}"
    "QLabel#rowTitle{font-weight:600;}"
    "QLabel#rowDescription,QLabel#statusText,QLabel#noteText{font-weight:400;}"
    "QLabel#sectionLabel{font-weight:600;}";

static QTableWidgetItem *makeVocabularyItem(const QString &text = QString());

class VocabularyTable : public QTableWidget {
public:
    explicit VocabularyTable(QWidget *parent = nullptr)
        : QTableWidget(parent)
    {
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
            const int nextRow = qMax(currentRow() + 1, 0);
            if (nextRow >= rowCount()) {
                insertRow(rowCount());
                setItem(rowCount() - 1, 0, makeVocabularyItem());
            }
            setCurrentCell(nextRow, 0);
            editItem(item(nextRow, 0));
            event->accept();
            return;
        }
        QTableWidget::keyPressEvent(event);
    }
};

static QTableWidgetItem *makeVocabularyItem(const QString &text)
{
    auto *item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled);
    return item;
}

static QFrame *makeSeparator(QWidget *parent)
{
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("settingsSeparator"));
    line->setFixedHeight(1);
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return line;
}

static QFrame *makeRow(const QString &label, const QString &description, QWidget *control, QWidget *parent)
{
    auto *row = new QFrame(parent);
    row->setObjectName(QStringLiteral("settingsRow"));
    row->setMinimumHeight(92);
    row->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(24);

    auto *text = new QWidget(row);
    text->setObjectName(QStringLiteral("rowText"));
    text->setAttribute(Qt::WA_StyledBackground, false);
    text->setAutoFillBackground(false);
    text->setMinimumWidth(280);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(4);

    auto *title = new QLabel(label, text);
    title->setObjectName(QStringLiteral("rowTitle"));
    auto *subtitle = new QLabel(description, text);
    subtitle->setObjectName(QStringLiteral("rowDescription"));
    subtitle->setWordWrap(true);
    title->setAttribute(Qt::WA_StyledBackground, false);
    subtitle->setAttribute(Qt::WA_StyledBackground, false);
    title->setAutoFillBackground(false);
    subtitle->setAutoFillBackground(false);
    title->setForegroundRole(QPalette::WindowText);
    subtitle->setForegroundRole(QPalette::WindowText);
    textLayout->addWidget(title);
    if (!description.isEmpty()) {
        textLayout->addWidget(subtitle);
    }

    if (auto *labelControl = qobject_cast<QLabel *>(control)) {
        labelControl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        labelControl->setWordWrap(false);
        labelControl->setAttribute(Qt::WA_StyledBackground, false);
        labelControl->setAutoFillBackground(false);
        labelControl->setMinimumWidth(170);
    } else {
        control->setMinimumWidth(180);
        control->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    }

    layout->addWidget(text, 1, Qt::AlignVCenter);
    layout->addStretch();
    layout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
    return row;
}

static void addRow(QVBoxLayout *layout, QFrame *row, QWidget *parent, bool addSeparator = true)
{
    layout->addWidget(row);
    if (addSeparator) {
        layout->addWidget(makeSeparator(parent));
    }
}

static void selectData(QComboBox *combo, const QString &data)
{
    const int index = combo->findData(data);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

static void setComboItemEnabled(QComboBox *combo, int index, bool enabled, const QString &toolTip = QString())
{
    auto *model = qobject_cast<QStandardItemModel *>(combo->model());
    if (!model || index < 0) {
        return;
    }
    QStandardItem *item = model->item(index);
    if (!item) {
        return;
    }
    item->setEnabled(enabled);
    item->setToolTip(toolTip);
}

static QWidget *makeYdotoolControl(QLabel *status,
                                   QPushButton *setup,
                                   QPushButton *start,
                                   QPushButton *disable,
                                   QPushButton *remove,
                                   QWidget *parent)
{
    auto *control = new QWidget(parent);
    auto *layout = new QVBoxLayout(control);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    status->setObjectName(QStringLiteral("statusText"));
    status->setWordWrap(true);
    status->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    status->setForegroundRole(QPalette::WindowText);
    status->setAttribute(Qt::WA_StyledBackground, false);

    auto *row = new QHBoxLayout();
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);
    row->addWidget(setup);
    row->addWidget(start);
    row->addWidget(disable);
    row->addWidget(remove);

    layout->addWidget(status);
    layout->addLayout(row);
    return control;
}

static QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
    auto *section = new QLabel(text, parent);
    section->setObjectName(QStringLiteral("sectionLabel"));
    section->setAttribute(Qt::WA_StyledBackground, false);
    section->setAlignment(Qt::AlignCenter);
    return section;
}

static QFrame *makeSettingsCard(QWidget *parent)
{
    auto *card = new QFrame(parent);
    card->setObjectName(QStringLiteral("settingsCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    return card;
}

SettingsDialog::SettingsDialog(ApplicationController *controller, QWidget *parent)
    : QDialog(parent)
    , m_controller(controller)
    , m_theme(new QComboBox(this))
    , m_pauseMedia(new QCheckBox(this))
    , m_provider(new QComboBox(this))
    , m_refinementStyle(new QComboBox(this))
    , m_outputFormat(new QComboBox(this))
    , m_outputMethod(new QComboBox(this))
    , m_authMode(new QComboBox(this))
    , m_authControl(new QStackedWidget(this))
    , m_authStatus(new QLabel(this))
    , m_ydotoolStatus(new QLabel(this))
    , m_vocabLimit(new QLabel(this))
    , m_apiKey(new QLineEdit(this))
    , m_previewWords(new QSpinBox(this))
    , m_vocab(new VocabularyTable(this))
{
    setWindowTitle(QStringLiteral("Speecher Settings"));
    resize(720, 780);
    setMinimumWidth(640);
    m_theme->addItem(QStringLiteral("System"), QStringLiteral("system"));
    m_theme->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    m_theme->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    m_pauseMedia->setText(QStringLiteral("Pause"));
    for (const ProviderDescriptor &provider : m_controller->providerRegistry()->refinementProviders()) {
        m_provider->addItem(provider.label, provider.id);
    }
    m_provider->addItem(QStringLiteral("None"), QStringLiteral("none"));
    m_refinementStyle->addItem(QStringLiteral("Strong polish"), QStringLiteral("strong_polish"));
    m_refinementStyle->addItem(QStringLiteral("Balanced"), QStringLiteral("balanced"));
    m_refinementStyle->addItem(QStringLiteral("Light cleanup"), QStringLiteral("light_cleanup"));
    m_outputFormat->addItem(QStringLiteral("Plain sentences"), QStringLiteral("plain_sentences"));
    m_outputFormat->addItem(QStringLiteral("Markdown style"), QStringLiteral("markdown"));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::Automatic)), QString::fromLatin1(OutputMethod::Automatic));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::Wtype)), QString::fromLatin1(OutputMethod::Wtype));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::Ydotool)), QString::fromLatin1(OutputMethod::Ydotool));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::WlCopy)), QString::fromLatin1(OutputMethod::WlCopy));
    m_outputMethod->addItem(OutputMethod::label(QString::fromLatin1(OutputMethod::QtClipboard)), QString::fromLatin1(OutputMethod::QtClipboard));
    m_outputMethod->setToolTip(QStringLiteral("How Speecher delivers final text."));
    m_outputMethod->view()->setMouseTracking(true);
    m_authMode->addItem(QStringLiteral("Automatic"), QStringLiteral("auto"));
    m_authMode->addItem(QStringLiteral("Codex API key"), QStringLiteral("codex_api_key"));
    m_authMode->addItem(QStringLiteral("Codex OAuth"), QStringLiteral("codex_oauth"));
    m_authMode->addItem(QStringLiteral("OPENAI_API_KEY"), QStringLiteral("env"));
    m_authMode->addItem(QStringLiteral("App settings key"), QStringLiteral("settings"));
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(QStringLiteral("Enter OpenAI API key"));
    m_authControl->addWidget(m_authStatus);
    m_authControl->addWidget(m_apiKey);
    m_previewWords->setRange(1, 40);
    m_vocab->setObjectName(QStringLiteral("vocabInput"));
    m_vocab->setColumnCount(1);
    m_vocab->setHorizontalHeaderLabels({QStringLiteral("Term")});
    m_vocab->horizontalHeader()->hide();
    m_vocab->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_vocab->verticalHeader()->hide();
    m_vocab->setShowGrid(true);
    m_vocab->setAlternatingRowColors(false);
    m_vocab->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_vocab->setSelectionMode(QAbstractItemView::SingleSelection);
    m_vocab->setEditTriggers(QAbstractItemView::AllEditTriggers);
    m_vocab->setTabKeyNavigation(false);
    m_vocab->setMinimumHeight(120);
    m_vocabLimit->setObjectName(QStringLiteral("statusText"));
    m_vocabLimit->setForegroundRole(QPalette::WindowText);
    m_vocabLimit->setAttribute(Qt::WA_StyledBackground, false);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(12);

    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setAutoFillBackground(false);
    scroll->viewport()->setAutoFillBackground(false);

    auto *scrollContent = new QWidget(scroll);
    auto *content = new QVBoxLayout(scrollContent);
    content->setContentsMargins(0, 0, 0, 0);
    content->setSpacing(12);

    auto *generalSection = makeSectionLabel(QStringLiteral("General"), this);
    auto *refinementSection = makeSectionLabel(QStringLiteral("Refinement"), this);
    auto *outputSection = makeSectionLabel(QStringLiteral("Output"), this);
    auto *openAiSection = makeSectionLabel(QStringLiteral("OpenAI"), this);
    auto *vocabularySection = makeSectionLabel(QStringLiteral("Vocabulary"), this);

    auto *generalCard = makeSettingsCard(this);
    auto *generalLayout = qobject_cast<QVBoxLayout *>(generalCard->layout());
    auto *refinementCard = makeSettingsCard(this);
    auto *refinementLayout = qobject_cast<QVBoxLayout *>(refinementCard->layout());
    auto *outputCard = makeSettingsCard(this);
    auto *outputLayout = qobject_cast<QVBoxLayout *>(outputCard->layout());
    auto *openAiCard = makeSettingsCard(this);
    auto *openAiLayout = qobject_cast<QVBoxLayout *>(openAiCard->layout());

    auto *model = new QLabel(QStringLiteral("gpt-5.4-mini"), this);
    auto *primaryOutput = new QLabel(m_controller->primaryOutputStatus(), this);
    m_ydotoolSetupButton = new QPushButton(QStringLiteral("Set up"), this);
    m_ydotoolStartButton = new QPushButton(QStringLiteral("Start service"), this);
    m_ydotoolDisableButton = new QPushButton(QStringLiteral("Disable in Speecher"), this);
    m_ydotoolRemoveButton = new QPushButton(QStringLiteral("Remove setup"), this);
    for (QPushButton *button : {m_ydotoolSetupButton, m_ydotoolStartButton, m_ydotoolDisableButton, m_ydotoolRemoveButton}) {
        button->setMinimumWidth(button->fontMetrics().horizontalAdvance(button->text()) + 36);
        button->setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
    }
    m_authStatus->setObjectName(QStringLiteral("statusText"));
    m_authStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_authStatus->setWordWrap(false);
    m_authStatus->setMinimumWidth(170);
    m_authStatus->setAttribute(Qt::WA_StyledBackground, false);
    m_authStatus->setAutoFillBackground(false);
    model->setObjectName(QStringLiteral("statusText"));
    primaryOutput->setObjectName(QStringLiteral("statusText"));
    for (QLabel *label : {m_authStatus, model, primaryOutput}) {
        label->setForegroundRole(QPalette::WindowText);
    }

    addRow(generalLayout, makeRow(QStringLiteral("Theme"), QStringLiteral("App colors."), m_theme, generalCard), generalCard);
    addRow(generalLayout, makeRow(QStringLiteral("Pause media"), QStringLiteral("Pause currently playing media while transcribing."), m_pauseMedia, generalCard), generalCard);
    addRow(generalLayout, makeRow(QStringLiteral("Preview words"), QStringLiteral("Trailing words shown in the popup."), m_previewWords, generalCard), generalCard);
    addRow(generalLayout, makeRow(QStringLiteral("Primary output"), QStringLiteral("Current platform typing adapter."), primaryOutput, generalCard), generalCard, false);

    addRow(outputLayout,
           makeRow(QStringLiteral("Method"),
                   QStringLiteral("How Speecher delivers final text."),
                   m_outputMethod,
                   outputCard),
           outputCard);
    addRow(outputLayout,
           makeRow(QStringLiteral("Virtual keyboard"),
                   QString(),
                   makeYdotoolControl(m_ydotoolStatus,
                                      m_ydotoolSetupButton,
                                      m_ydotoolStartButton,
                                      m_ydotoolDisableButton,
                                      m_ydotoolRemoveButton,
                                      outputCard),
                   outputCard),
           outputCard,
           false);

    addRow(refinementLayout, makeRow(QStringLiteral("Refinement"), QStringLiteral("Clean up dictated text after capture."), m_provider, refinementCard), refinementCard);
    addRow(refinementLayout, makeRow(QStringLiteral("Refinement style"), QStringLiteral("How strongly dictated text is rewritten."), m_refinementStyle, refinementCard), refinementCard);
    addRow(refinementLayout, makeRow(QStringLiteral("Output format"), QStringLiteral("How lists and structure are formatted."), m_outputFormat, refinementCard), refinementCard, false);

    addRow(openAiLayout, makeRow(QStringLiteral("OpenAI model"), QStringLiteral("Model used for refinement."), model, openAiCard), openAiCard);
    addRow(openAiLayout, makeRow(QStringLiteral("OpenAI auth mode"), QStringLiteral("Credential source used for refinement."), m_authMode, openAiCard), openAiCard);
    addRow(openAiLayout, makeRow(QStringLiteral("OpenAI auth"), QStringLiteral("Current credential source or app settings key."), m_authControl, openAiCard), openAiCard, false);

    auto *vocabSection = new QFrame(this);
    vocabSection->setObjectName(QStringLiteral("vocabSection"));
    auto *vocabLayout = new QVBoxLayout(vocabSection);
    vocabLayout->setContentsMargins(20, 16, 20, 20);
    vocabLayout->setSpacing(8);
    auto *vocabTitle = new QLabel(QStringLiteral("Extra vocabulary"), vocabSection);
    vocabTitle->setObjectName(QStringLiteral("rowTitle"));
    vocabTitle->setAlignment(Qt::AlignCenter);
    vocabTitle->setForegroundRole(QPalette::WindowText);
    vocabTitle->setAttribute(Qt::WA_StyledBackground, false);
    auto *vocabDescription = new QLabel(QStringLiteral("One term per line. Claude voice uses Deepgram Nova-3 keyterms: 500 tokens and 100 keyterms maximum."), vocabSection);
    vocabDescription->setObjectName(QStringLiteral("rowDescription"));
    vocabDescription->setAlignment(Qt::AlignCenter);
    vocabDescription->setWordWrap(true);
    vocabDescription->setAttribute(Qt::WA_StyledBackground, false);
    vocabLayout->addWidget(vocabTitle);
    vocabLayout->addWidget(vocabDescription);
    vocabLayout->addWidget(m_vocab);
    vocabLayout->addWidget(m_vocabLimit);

    auto *note = new QLabel(QStringLiteral("Automatic OpenAI auth follows the Codex auth mode when available, then falls back to Codex API key, Codex OAuth, OPENAI_API_KEY, and the app settings key. Codex OAuth uses the ChatGPT Codex backend. The app settings key is stored in the desktop keyring through QtKeychain when available."), this);
    note->setObjectName(QStringLiteral("noteText"));
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::WindowText);
    note->setAttribute(Qt::WA_StyledBackground, false);

    content->addWidget(generalSection);
    content->addWidget(generalCard);
    content->addWidget(outputSection);
    content->addWidget(outputCard);
    content->addWidget(refinementSection);
    content->addWidget(refinementCard);
    content->addWidget(openAiSection);
    content->addWidget(openAiCard);
    content->addWidget(vocabularySection);
    content->addWidget(vocabSection);
    content->addWidget(note);
    scroll->setWidget(scrollContent);
    root->addWidget(scroll, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    buttons->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    root->addWidget(buttons, 0, Qt::AlignRight);

    generalCard->setStyleSheet(QString::fromLatin1(settingsStyle));
    outputCard->setStyleSheet(QString::fromLatin1(settingsStyle));
    refinementCard->setStyleSheet(QString::fromLatin1(settingsStyle));
    openAiCard->setStyleSheet(QString::fromLatin1(settingsStyle));
    vocabSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    generalSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    outputSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    refinementSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    openAiSection->setStyleSheet(QString::fromLatin1(settingsStyle));
    vocabularySection->setStyleSheet(QString::fromLatin1(settingsStyle));
    note->setStyleSheet(QString::fromLatin1(settingsStyle));

    if (QPushButton *ok = buttons->button(QDialogButtonBox::Ok)) {
        m_okButton = ok;
        ok->setDefault(true);
        ok->setAutoDefault(true);
        ok->setIcon(QIcon());
    }
    if (QPushButton *cancel = buttons->button(QDialogButtonBox::Cancel)) {
        cancel->setAutoDefault(false);
        cancel->setIcon(QIcon());
    }
    if (QPushButton *apply = buttons->button(QDialogButtonBox::Apply)) {
        m_applyButton = apply;
        apply->setAutoDefault(false);
        apply->setIcon(QIcon());
    }

    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(m_applyButton, &QPushButton::clicked, this, &SettingsDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_theme, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_pauseMedia, &QCheckBox::toggled, this, &SettingsDialog::updateButtonState);
    connect(m_provider, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_refinementStyle, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_outputFormat, &QComboBox::currentIndexChanged, this, &SettingsDialog::updateButtonState);
    connect(m_outputMethod, &QComboBox::currentIndexChanged, this, [this] {
        if (m_outputMethod->currentData().toString() == QString::fromLatin1(OutputMethod::Ydotool)) {
            const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
            if (!status.ready() || !m_controller->settings()->ydotoolEnabled()) {
                QSignalBlocker blocker(m_outputMethod);
                selectData(m_outputMethod, m_controller->settings()->outputMethod());
                QToolTip::showText(m_outputMethod->mapToGlobal(m_outputMethod->rect().bottomLeft()),
                                   QStringLiteral("Set up ydotool first"),
                                   m_outputMethod);
                return;
            }
        }
        updateButtonState();
    });
    connect(m_authMode, &QComboBox::currentIndexChanged, this, [this] {
        updateAuthControl();
        updateButtonState();
    });
    connect(m_apiKey, &QLineEdit::textChanged, this, &SettingsDialog::updateButtonState);
    connect(m_previewWords, &QSpinBox::valueChanged, this, &SettingsDialog::updateButtonState);
    connect(m_ydotoolSetupButton, &QPushButton::clicked, this, &SettingsDialog::setupOrEnableYdotool);
    connect(m_ydotoolStartButton, &QPushButton::clicked, this, [this] {
        QString error;
        if (!YdotoolSetup::startUserService(&error)) {
            QMessageBox::warning(this, QStringLiteral("ydotool service"), error);
        }
        refreshOutputControls();
    });
    connect(m_ydotoolDisableButton, &QPushButton::clicked, this, &SettingsDialog::disableYdotool);
    connect(m_ydotoolRemoveButton, &QPushButton::clicked, this, &SettingsDialog::removeYdotoolSetup);
    connect(m_vocab, &QTableWidget::itemChanged, this, [this] {
        updateVocabularyLimit();
        updateButtonState();
    });
    load();
}

void SettingsDialog::load()
{
    SettingsStore *settings = m_controller->settings();
    selectData(m_theme, settings->theme());
    m_pauseMedia->setChecked(settings->pauseMediaDuringTranscription());
    selectData(m_provider, settings->refinementProvider());
    selectData(m_refinementStyle, settings->refinementStyle());
    selectData(m_outputFormat, settings->refinementOutputFormat());
    selectData(m_outputMethod, settings->outputMethod());
    selectData(m_authMode, settings->openAiAuthMode());
    m_previewWords->setValue(settings->previewWords());
    m_apiKey->setText(m_controller->secretStore()->apiKey());
    setVocabularyRows(settings->customVocabulary());
    updateVocabularyLimit();
    updateAuthControl();
    refreshOutputControls();
    updateButtonState();
}

void SettingsDialog::save()
{
    SettingsStore *settings = m_controller->settings();
    settings->setTheme(m_theme->currentData().toString());
    Theme::apply(settings->theme());
    settings->setPauseMediaDuringTranscription(m_pauseMedia->isChecked());
    settings->setRefinementProvider(m_provider->currentData().toString());
    settings->setRefinementStyle(m_refinementStyle->currentData().toString());
    settings->setRefinementOutputFormat(m_outputFormat->currentData().toString());
    settings->setOutputMethod(m_outputMethod->currentData().toString());
    settings->setOpenAiAuthMode(m_authMode->currentData().toString());
    settings->setPreviewWords(m_previewWords->value());
    settings->setCustomVocabulary(currentVocabulary());
    setVocabularyRows(settings->customVocabulary());
    updateVocabularyLimit();
    if (settings->openAiAuthMode() == QStringLiteral("settings")) {
        if (!m_controller->secretStore()->saveApiKey(m_apiKey->text().trimmed())) {
            QMessageBox::warning(this,
                                 QStringLiteral("OpenAI key not saved"),
                                 m_controller->secretStore()->status());
            return;
        }
    }
    updateAuthControl();
    refreshOutputControls();
    updateButtonState();
}

bool SettingsDialog::hasChanges() const
{
    const SettingsStore *settings = m_controller->settings();
    if (m_theme->currentData().toString() != settings->theme()
        || m_pauseMedia->isChecked() != settings->pauseMediaDuringTranscription()
        || m_provider->currentData().toString() != settings->refinementProvider()
        || m_refinementStyle->currentData().toString() != settings->refinementStyle()
        || m_outputFormat->currentData().toString() != settings->refinementOutputFormat()
        || m_outputMethod->currentData().toString() != settings->outputMethod()
        || m_authMode->currentData().toString() != settings->openAiAuthMode()
        || m_previewWords->value() != settings->previewWords()
        || currentVocabulary() != settings->customVocabulary()) {
        return true;
    }

    return m_authMode->currentData().toString() == QStringLiteral("settings")
        && m_apiKey->text().trimmed() != m_controller->secretStore()->apiKey();
}

QStringList SettingsDialog::currentVocabulary() const
{
    QStringList vocabulary;
    for (int row = 0; row < m_vocab->rowCount(); ++row) {
        const QTableWidgetItem *item = m_vocab->item(row, 0);
        const QString term = item ? item->text().trimmed() : QString();
        if (!term.isEmpty()) {
            vocabulary << term;
        }
    }
    return VocabularyLimit::limited(vocabulary);
}

void SettingsDialog::setVocabularyRows(const QStringList &terms)
{
    QSignalBlocker blocker(m_vocab);
    m_updatingVocabulary = true;

    m_vocab->clearContents();
    m_vocab->setRowCount(qMax(terms.size() + 1, 1));
    for (int row = 0; row < m_vocab->rowCount(); ++row) {
        m_vocab->setItem(row, 0, makeVocabularyItem(row < terms.size() ? terms.at(row) : QString()));
    }
    m_vocab->setCurrentCell(0, 0);

    m_updatingVocabulary = false;
}

void SettingsDialog::updateAuthControl()
{
    const QString mode = m_authMode->currentData().toString();
    if (mode == QStringLiteral("settings")) {
        m_authControl->setCurrentWidget(m_apiKey);
        m_apiKey->setPlaceholderText(m_controller->secretStore()->status());
        return;
    }
    m_authStatus->setText(OpenAiAuthProvider(m_controller->secretStore(), mode).status());
    m_authControl->setCurrentWidget(m_authStatus);
}

void SettingsDialog::refreshOutputControls()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
    const bool ydotoolEnabled = m_controller->settings()->ydotoolEnabled() && status.ready();
    const int ydotoolIndex = m_outputMethod->findData(QString::fromLatin1(OutputMethod::Ydotool));
    setComboItemEnabled(m_outputMethod,
                        ydotoolIndex,
                        ydotoolEnabled,
                        ydotoolEnabled ? QString() : QStringLiteral("Set up ydotool first"));
    if (!ydotoolEnabled && m_outputMethod->currentData().toString() == QString::fromLatin1(OutputMethod::Ydotool)) {
        QSignalBlocker blocker(m_outputMethod);
        selectData(m_outputMethod, QString::fromLatin1(OutputMethod::Automatic));
    }
    m_outputMethod->setToolTip(ydotoolEnabled
                                   ? QStringLiteral("Automatic tries wtype, ydotool, wl-copy, then Qt clipboard.")
                                   : QStringLiteral("Type with ydotool is visible but disabled until virtual keyboard setup passes."));
    m_ydotoolStatus->setText(status.label + QStringLiteral(". ") + status.detail);
    updateYdotoolButtons();
}

void SettingsDialog::updateYdotoolButtons()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
    const bool ready = status.ready();
    const bool disabled = status.state == YdotoolSetupState::Disabled;
    const bool daemonMissing = status.state == YdotoolSetupState::DaemonNotRunning;
    const bool setupInstalled = status.speecherManagedSetupInstalled || ready || disabled;
    const QString setupFirst = QStringLiteral("Run setup first");
    m_ydotoolSetupButton->setText(disabled && status.speecherManagedSetupInstalled ? QStringLiteral("Enable") : QStringLiteral("Set up"));
    m_ydotoolSetupButton->setVisible(!ready || disabled);
    m_ydotoolSetupButton->setEnabled(status.state != YdotoolSetupState::NeedsSignOut);
    m_ydotoolStartButton->setVisible(daemonMissing);
    m_ydotoolStartButton->setEnabled(setupInstalled);
    m_ydotoolStartButton->setToolTip(setupInstalled ? QString() : setupFirst);
    m_ydotoolDisableButton->setVisible(ready && m_controller->settings()->ydotoolEnabled());
    m_ydotoolRemoveButton->setVisible(status.speecherManagedSetupInstalled);
    m_ydotoolRemoveButton->setEnabled(status.speecherManagedSetupInstalled);
    m_ydotoolRemoveButton->setToolTip(setupInstalled ? QString() : setupFirst);
}

void SettingsDialog::updateButtonState()
{
    const bool changed = hasChanges();
    if (m_okButton) {
        m_okButton->setEnabled(changed);
    }
    if (m_applyButton) {
        m_applyButton->setEnabled(changed);
    }
}

void SettingsDialog::setupOrEnableYdotool()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_controller->settings()->ydotoolEnabled());
    if (status.state == YdotoolSetupState::Disabled && status.speecherManagedSetupInstalled) {
        if (verifyYdotoolTyping()) {
            m_controller->settings()->setYdotoolEnabled(true);
            refreshOutputControls();
            updateButtonState();
        }
        return;
    }

    const int answer = QMessageBox::question(this,
                                             QStringLiteral("Set up virtual keyboard"),
                                             QStringLiteral("Speecher will ask for administrator permission to install ydotool if needed, load uinput, configure a speecher-uinput group, install udev rules, and install a user-level ydotoold service. Speecher itself remains unprivileged at runtime."),
                                             QMessageBox::Cancel | QMessageBox::Ok,
                                             QMessageBox::Ok);
    if (answer != QMessageBox::Ok) {
        return;
    }

    QString error;
    if (!YdotoolSetup::runHelper(YdotoolSetup::HelperAction::Install, &error)) {
        QMessageBox::warning(this, QStringLiteral("ydotool setup failed"), error);
        refreshOutputControls();
        return;
    }
    if (!YdotoolSetup::startUserService(&error)) {
        QMessageBox::warning(this, QStringLiteral("ydotool service"), error);
    }
    if (verifyYdotoolTyping()) {
        m_controller->settings()->setYdotoolEnabled(true);
    }
    refreshOutputControls();
    updateButtonState();
}

void SettingsDialog::disableYdotool()
{
    m_controller->settings()->setYdotoolEnabled(false);
    QSignalBlocker blocker(m_outputMethod);
    selectData(m_outputMethod, m_controller->settings()->outputMethod());
    refreshOutputControls();
    updateButtonState();
}

void SettingsDialog::removeYdotoolSetup()
{
    const int answer = QMessageBox::question(this,
                                             QStringLiteral("Remove virtual keyboard setup"),
                                             QStringLiteral("Speecher will ask for administrator permission to remove the service, udev rule, module-load file, and Speecher-specific group membership it manages. It will not uninstall the distro ydotool package."),
                                             QMessageBox::Cancel | QMessageBox::Ok,
                                             QMessageBox::Cancel);
    if (answer != QMessageBox::Ok) {
        return;
    }
    QString error;
    QString stopError;
    YdotoolSetup::stopUserService(&stopError);
    if (!YdotoolSetup::runHelper(YdotoolSetup::HelperAction::Remove, &error)) {
        QMessageBox::warning(this, QStringLiteral("ydotool removal failed"), error);
        refreshOutputControls();
        return;
    }

    const YdotoolSetupStatus status = YdotoolSetup::probe(false);
    if (status.speecherManagedSetupInstalled) {
        QMessageBox::warning(this,
                             QStringLiteral("ydotool removal incomplete"),
                             QStringLiteral("The privileged helper finished, but Speecher-managed setup files are still detected."));
        refreshOutputControls();
        return;
    }
    m_controller->settings()->setYdotoolEnabled(false);
    QSignalBlocker blocker(m_outputMethod);
    selectData(m_outputMethod, m_controller->settings()->outputMethod());
    refreshOutputControls();
    updateButtonState();
}

bool SettingsDialog::verifyYdotoolTyping()
{
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Verify ydotool"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(QStringLiteral("Keep this field focused while Speecher tests virtual keyboard input."), &dialog);
    label->setWordWrap(true);
    auto *field = new QLineEdit(&dialog);
    field->setClearButtonEnabled(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *run = buttons->addButton(QStringLiteral("Run test"), QDialogButtonBox::AcceptRole);
    layout->addWidget(label);
    layout->addWidget(field);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(run, &QPushButton::clicked, &dialog, [field, &dialog] {
        field->clear();
        field->setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(150, field, [field, &dialog] {
            QString error;
            YdotoolDelivery ydotool;
            const QString expected = QStringLiteral("speecher test");
            if (!ydotool.type(expected, &error)) {
                QMessageBox::warning(&dialog, QStringLiteral("ydotool verification failed"), error);
                return;
            }
            QTimer::singleShot(350, field, [field, expected, &dialog] {
                if (field->text() == expected) {
                    dialog.accept();
                } else {
                    QMessageBox::warning(&dialog,
                                         QStringLiteral("ydotool verification failed"),
                                         QStringLiteral("The test field did not receive the expected text."));
                }
            });
        });
    });
    dialog.resize(420, dialog.sizeHint().height());
    field->setFocus(Qt::OtherFocusReason);
    return dialog.exec() == QDialog::Accepted;
}

void SettingsDialog::updateVocabularyLimit()
{
    if (m_updatingVocabulary) {
        return;
    }

    QStringList vocabulary;
    for (int row = 0; row < m_vocab->rowCount(); ++row) {
        const QTableWidgetItem *item = m_vocab->item(row, 0);
        vocabulary << (item ? item->text() : QString());
    }
    QStringList terms;
    for (const QString &line : vocabulary) {
        const QString term = line.trimmed();
        if (!term.isEmpty()) {
            terms << term;
        }
    }

    QStringList uniqueTerms;
    for (const QString &term : vocabulary) {
        const QString normalized = term.simplified();
        if (!normalized.isEmpty() && !uniqueTerms.contains(normalized)) {
            uniqueTerms << normalized;
        }
    }

    const QStringList limited = VocabularyLimit::limited(vocabulary);
    if (limited != terms
        && (VocabularyLimit::tokenCount(uniqueTerms) > VocabularyLimit::maxTokens || uniqueTerms.size() > VocabularyLimit::maxKeyterms)) {
        setVocabularyRows(limited);
    } else if (m_vocab->rowCount() == 0 || !m_vocab->item(m_vocab->rowCount() - 1, 0)
               || !m_vocab->item(m_vocab->rowCount() - 1, 0)->text().trimmed().isEmpty()) {
        const QSignalBlocker blocker(m_vocab);
        m_vocab->insertRow(m_vocab->rowCount());
        m_vocab->setItem(m_vocab->rowCount() - 1, 0, makeVocabularyItem());
    }

    m_vocabLimit->setText(VocabularyLimit::summary(currentVocabulary()));
}

} // namespace speecher
