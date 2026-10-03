#include "ui/TranscribePage.h"

#include "app/ApplicationController.h"
#include "transcribe/TranscribePresentation.h"
#include "core/SettingsStore.h"
#include "core/Target.h"
#include "providers/ProviderRegistry.h"
#include "ui/InlineMessage.h"
#include "ui/TranscribeLoomWidget.h"
#include "ui/TranscribeModel.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace speecher {
namespace {

// What a cleanup level, tone or profile does, kept on its combo item.
constexpr int kSelectionHelpRole = Qt::UserRole + 1;

// The row's description says what the chosen item does, as the profile
// dialog says under the same choices.
void showSelectionHelp(QComboBox *combo)
{
    auto *description = combo->parentWidget()->findChild<QLabel *>(QStringLiteral("rowDescription"));
    const QString help = combo->currentData(kSelectionHelpRole).toString();
    description->setText(help);
    description->setVisible(!help.isEmpty());
    combo->setAccessibleDescription(help);
}

QIcon themedIcon(const QString &name, const QString &fallback)
{
    return QIcon::fromTheme(name, QIcon::fromTheme(fallback));
}

// Removes every card row after the first `keep`, with their separators.
void clearCardRows(QFrame *card, int keep)
{
    QFormLayout *form = settings::cardFormLayout(card);
    while (form->rowCount() > keep) {
        form->removeRow(keep);
    }
}

// Shows or hides a card row together with the hairline above it; hiding the
// row widget alone would leave a doubled separator.
void setCardRowVisible(QWidget *row, bool visible)
{
    auto *form = qobject_cast<QFormLayout *>(row->parentWidget()->layout());
    int index = -1;
    QFormLayout::ItemRole role;
    form->getWidgetPosition(row, &index, &role);
    form->setRowVisible(index, visible);
    if (index > 0) {
        form->setRowVisible(index - 1, visible);
    }
}

QLabel *dimLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setFont(settings::smallFont(label->font()));
    return label;
}

// A card row laid out left to right, with the row padding and spacing.
QWidget *plainRow(QWidget *parent, QHBoxLayout **layout)
{
    auto *row = new QWidget(parent);
    *layout = new QHBoxLayout(row);
    (*layout)->setContentsMargins(settings::rowPadding());
    (*layout)->setSpacing(settings::relatedSpacing());
    return row;
}

QToolButton *textButton(const QString &text, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setAutoRaise(true);
    return button;
}

QString writeText(const QString &path, const QString &text)
{
    QSaveFile file(path);
    const QByteArray bytes = text.toUtf8() + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return transcriptSaveError(path, file.errorString());
    }
    return {};
}

// A label that fits whatever width its row gives it, shortening its text with
// an ellipsis where mode puts it; the whole text shows in the tooltip.
class ElidingLabel final : public QLabel {
public:
    ElidingLabel(const QString &text, Qt::TextElideMode mode, QWidget *parent)
        : QLabel(text, parent)
        , m_text(text)
        , m_mode(mode)
    {
        setMinimumWidth(fontMetrics().averageCharWidth() * 6);
        setToolTip(text);
    }

    QSize sizeHint() const override
    {
        return {fontMetrics().horizontalAdvance(m_text) + contentsMargins().left() + contentsMargins().right(),
                QLabel::sizeHint().height()};
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        setText(fontMetrics().elidedText(m_text, m_mode, contentsRect().width()));
    }

private:
    QString m_text;
    Qt::TextElideMode m_mode;
};

// A file name that loses its middle, keeping the start and the extension.
QLabel *fileNameLabel(const QString &path, QWidget *parent)
{
    auto *label = new ElidingLabel(QFileInfo(path).fileName(), Qt::ElideMiddle, parent);
    label->setToolTip(QDir::toNativeSeparators(path));
    return label;
}

// A result's heading: the expander and file name, then its details (length,
// Saved, the actions), on one line or with the details stacked under the
// name, indented to its first letter. The page chooses, the same for every
// result, from the width the widest needs on one line.
class ResultHead final : public QWidget {
public:
    ResultHead(QWidget *lead, QLabel *name, QWidget *details, QWidget *parent)
        : QWidget(parent)
        , m_lead(lead)
        , m_name(name)
        , m_details(details)
        , m_layout(new QBoxLayout(QBoxLayout::LeftToRight, this))
    {
        setObjectName(QStringLiteral("transcribeResultHead"));
        m_layout->setContentsMargins(settings::rowPadding());
        m_layout->setSpacing(settings::relatedSpacing());
        auto *nameLine = new QHBoxLayout;
        nameLine->setSpacing(settings::relatedSpacing());
        nameLine->addWidget(lead);
        nameLine->addWidget(name, 1);
        m_layout->addLayout(nameLine, 1);
        m_layout->addWidget(details);
    }

    // The width this head needs on one line with about 24 characters of name
    // showing, enough to tell files apart.
    int oneLineWidth() const
    {
        constexpr int kNameCharacters = 24;
        const QMargins margins = m_layout->contentsMargins();
        return margins.left() + m_lead->sizeHint().width() + m_name->fontMetrics().averageCharWidth() * kNameCharacters
            + m_details->sizeHint().width() - m_details->contentsMargins().left() + m_layout->spacing() * 2
            + margins.right();
    }

    void setStacked(bool stacked)
    {
        m_layout->setDirection(stacked ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        m_details->setContentsMargins(stacked ? m_lead->sizeHint().width() + m_layout->spacing() : 0, 0, 0, 0);
    }

private:
    QWidget *m_lead;
    QLabel *m_name;
    QWidget *m_details;
    QBoxLayout *m_layout;
};

} // namespace

TranscribePage::TranscribePage(ApplicationController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_model(TranscribeModel::of(controller))
{
    setAcceptDrops(true);
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    auto *content = new QWidget(scroll);
    auto *column = new QVBoxLayout(content);
    settings::applyPageMargins(column);
    column->setSpacing(0);
    settings::configurePageScroll(scroll, content);
    outer->addWidget(scroll);

    const auto addCard = [](QVBoxLayout *layout, const QString &title, QWidget *parent) {
        if (layout->count() > 0) {
            layout->addSpacing(settings::groupGap());
        }
        layout->addWidget(settings::makeSectionLabel(title, parent));
        QFrame *card = settings::makeSettingsCard(parent);
        layout->addWidget(card);
        return card;
    };
    const auto addCentered = [](QVBoxLayout *layout, QWidget *widget) {
        layout->addSpacing(settings::sectionGap());
        layout->addWidget(widget, 0, Qt::AlignHCenter);
    };
    // ---- Steps ----
    auto *steps = new QHBoxLayout;
    steps->setSpacing(settings::relatedSpacing());
    steps->addStretch();
    for (TranscribeStep step : {TranscribeStep::Configure, TranscribeStep::Transcribe, TranscribeStep::Export}) {
        if (!m_stepLabels.isEmpty()) {
            steps->addWidget(dimLabel(QStringLiteral("·"), content));
        }
        auto *label = new QLabel(transcribeStepLabel(step), content);
        m_stepLabels << label;
        steps->addWidget(label);
    }
    steps->addStretch();
    column->addLayout(steps);
    m_stepHint = dimLabel(QString(), content);
    m_stepHint->setAlignment(Qt::AlignHCenter);
    column->addSpacing(settings::smallSpacing());
    column->addWidget(m_stepHint);
    column->addSpacing(settings::groupGap());

    // ---- Setup ----
    m_setup = new QWidget(content);
    auto *setup = new QVBoxLayout(m_setup);
    setup->setContentsMargins(0, 0, 0, 0);
    setup->setSpacing(0);

    m_filesCard = addCard(setup, transcribeText(TranscribeText::AudioFilesSection), m_setup);
    // Its caption follows the list; see refreshFileList.
    QPushButton *choose = settings::makeButtonRow(QString(), mediaFilesHint(), m_filesCard);
    choose->setObjectName(QStringLiteral("transcribeChooseFiles"));
    choose->setToolTip(mediaFilesTooltip());
    settings::addCardRow(settings::cardFormLayout(m_filesCard), choose, m_filesCard);
    connect(choose, &QPushButton::clicked, this, [this] {
        addFiles(QFileDialog::getOpenFileNames(
            this, transcribeText(TranscribeText::FilesDialogTitle), QDir::homePath(),
            QStringLiteral("Audio and video files (*.%1);;All files (*)")
                .arg(transcribableExtensions().join(QStringLiteral(" *.")))));
    });

    QFrame *speechCard = addCard(setup, transcribeText(TranscribeText::TranscriptionSection), m_setup);
    m_speech = new QComboBox(speechCard);
    for (const ProviderDescriptor &provider : m_controller->providerRegistry()->speechProviders()) {
        m_speech->addItem(provider.label, provider.id);
        m_speech->setItemData(m_speech->count() - 1, provider.summary, Qt::ToolTipRole);
    }
    QFrame *speechRow = settings::makeRow(transcribeText(TranscribeText::Service), QString(), m_speech, speechCard, nullptr, true);
    m_speechSummary = speechRow->findChild<QLabel *>(QStringLiteral("rowDescription"));
    settings::addCardRow(settings::cardFormLayout(speechCard), speechRow, speechCard);
    connect(m_speech, &QComboBox::currentIndexChanged, this, [this] {
        const QString summary = m_speech->currentData(Qt::ToolTipRole).toString();
        m_speechSummary->setText(summary);
        m_speechSummary->setVisible(!summary.isEmpty());
    });
    m_vocabulary = new QCheckBox(speechCard);
    settings::addCardRow(settings::cardFormLayout(speechCard),
                         settings::makeRow(transcribeText(TranscribeText::Vocabulary),
                                           transcribeText(TranscribeText::VocabularyHelp), m_vocabulary, speechCard),
                         speechCard);

    QFrame *refineCard = addCard(setup, transcribeText(TranscribeText::RefinementSection), m_setup);
    QFormLayout *refineForm = settings::cardFormLayout(refineCard);
    m_refiner = new QComboBox(refineCard);
    m_refiner->addItem(transcribeText(TranscribeText::NoRefiner), QStringLiteral("none"));
    for (const ProviderDescriptor &provider : m_controller->providerRegistry()->refinementProviders()) {
        m_refiner->addItem(provider.label, provider.id);
    }
    settings::addCardRow(refineForm,
                         settings::makeRow(transcribeText(TranscribeText::Refiner),
                                           transcribeText(TranscribeText::RefinerHelp),
                                           m_refiner, refineCard),
                         refineCard);
    // The model is set in Refinement settings, so the row opens them, with
    // the model it will use as its value.
    m_refinerModelRow = settings::makeButtonRow(transcribeText(TranscribeText::RefinerModel),
                                                QString(), refineCard, true);
    m_refinerModelRow->setObjectName(QStringLiteral("transcribeRefinerModel"));
    m_refinerModelRow->setToolTip(refinementModelHint());
    settings::addCardRow(refineForm, m_refinerModelRow, refineCard);
    connect(m_refinerModelRow, &QPushButton::clicked, this,
            [this] { emit pageRequested(QStringLiteral("refinement")); });

    // The profile comes first: it sets the cleanup level and tone below it.
    m_profile = new QComboBox(refineCard);
    QFrame *profileRow = settings::makeRow(transcribeText(TranscribeText::WritingProfile),
                                           transcribeText(TranscribeText::WritingProfileHelp),
                                           m_profile, refineCard);
    settings::addCardRow(refineForm, profileRow, refineCard);
    m_cleanup = new QComboBox(refineCard);
    m_cleanup->setObjectName(QStringLiteral("transcribeCleanup"));
    QFrame *cleanupRow = settings::makeRow(transcribeText(TranscribeText::Cleanup),
                                           transcribeText(TranscribeText::CleanupHelp),
                                           m_cleanup, refineCard);
    settings::addCardRow(refineForm, cleanupRow, refineCard);
    m_tone = new QComboBox(refineCard);
    QFrame *toneRow = settings::makeRow(transcribeText(TranscribeText::Tone),
                                        transcribeText(TranscribeText::ToneHelp),
                                        m_tone, refineCard);
    settings::addCardRow(refineForm, toneRow, refineCard);
    m_refinementDependents = {profileRow, cleanupRow, toneRow};
    connect(m_refiner, &QComboBox::currentIndexChanged, this, &TranscribePage::refreshRefinementRows);
    connect(m_profile, &QComboBox::currentIndexChanged, this, &TranscribePage::applyWritingProfile);
    for (QComboBox *combo : {m_profile, m_cleanup, m_tone}) {
        connect(combo, &QComboBox::currentIndexChanged, this, [combo] { showSelectionHelp(combo); });
    }

    QFrame *outputCard = addCard(setup, transcribeText(TranscribeText::OutputSection), m_setup);
    m_destination = new QComboBox(outputCard);
    for (TranscriptDestination destination :
         {TranscriptDestination::BesideInput, TranscriptDestination::Folder, TranscriptDestination::None}) {
        m_destination->addItem(destinationLabel(destination), int(destination));
    }
    QFrame *destinationRow = settings::makeRow(transcribeText(TranscribeText::SaveTranscripts),
                                               destinationHint(TranscriptDestination::BesideInput),
                                               m_destination, outputCard);
    m_destinationSummary = destinationRow->findChild<QLabel *>(QStringLiteral("rowDescription"));
    settings::addCardRow(settings::cardFormLayout(outputCard), destinationRow, outputCard);
    auto *changeFolder = new QPushButton(transcribeText(TranscribeText::ChangeFolder), outputCard);
    m_folderRow = settings::makeRow(transcribeText(TranscribeText::Folder), QStringLiteral(" "), changeFolder, outputCard);
    m_folderPath = m_folderRow->findChild<QLabel *>(QStringLiteral("rowDescription"));
    settings::addCardRow(settings::cardFormLayout(outputCard), m_folderRow, outputCard);
    const auto chooseFolder = [this] {
        const QString folder = QFileDialog::getExistingDirectory(
            this, transcribeText(TranscribeText::FolderDialogTitle), m_folder.isEmpty() ? QDir::homePath() : m_folder);
        if (!folder.isEmpty()) {
            m_folder = folder;
        }
    };
    connect(changeFolder, &QPushButton::clicked, this, [this, chooseFolder] {
        chooseFolder();
        refreshOutputRows();
    });
    connect(m_destination, &QComboBox::currentIndexChanged, this, [this, chooseFolder] {
        if (TranscriptDestination(m_destination->currentData().toInt()) == TranscriptDestination::Folder
            && m_folder.isEmpty()) {
            chooseFolder();
            if (m_folder.isEmpty()) {
                const QSignalBlocker blocker(m_destination);
                m_destination->setCurrentIndex(0);
            }
        }
        refreshOutputRows();
    });

    m_startError = new InlineMessage(m_setup);
    m_startError->setType(InlineMessage::Type::Error);
    m_startError->hide();
    setup->addSpacing(settings::groupGap());
    setup->addWidget(m_startError);
    m_start = new QPushButton(startCaption(0), m_setup);
    m_start->setObjectName(QStringLiteral("transcribeStart"));
    m_start->setDefault(true);
    m_start->setMinimumWidth(160);
    connect(m_start, &QPushButton::clicked, this, &TranscribePage::startBatch);
    addCentered(setup, m_start);
    // Why the button is disabled, right under it.
    m_noFiles = dimLabel(transcribeText(TranscribeText::NoFilesYet), m_setup);
    m_noFiles->setObjectName(QStringLiteral("transcribeNoFiles"));
    setup->addSpacing(settings::smallSpacing());
    setup->addWidget(m_noFiles, 0, Qt::AlignHCenter);
    column->addWidget(m_setup);

    // ---- Processing ----
    m_processing = new QWidget(content);
    auto *processing = new QVBoxLayout(m_processing);
    processing->setContentsMargins(0, 0, 0, 0);
    processing->setSpacing(0);
    m_processingHeader = settings::makeSectionLabel(processingTitle({}, -1), m_processing);
    processing->addWidget(m_processingHeader);
    m_queueCard = settings::makeSettingsCard(m_processing);
    processing->addWidget(m_queueCard);
    auto *stage = new QWidget(m_queueCard);
    auto *stageLayout = new QVBoxLayout(stage);
    stageLayout->setContentsMargins(settings::rowPadding());
    stageLayout->setSpacing(settings::smallSpacing());
    m_loom = new TranscribeLoomWidget(stage);
    stageLayout->addWidget(m_loom);
    auto *statusLine = new QHBoxLayout;
    m_phase = new QLabel(stage);
    m_percent = dimLabel(QString(), stage);
    statusLine->addWidget(m_phase);
    statusLine->addStretch();
    statusLine->addWidget(m_percent);
    stageLayout->addLayout(statusLine);
    // What the provider has heard so far, newest words in view, so a long
    // file visibly moves between percentage steps.
    m_partial = new QPlainTextEdit(stage);
    m_partial->setObjectName(QStringLiteral("transcribePartial"));
    m_partial->setAccessibleName(transcribeText(TranscribeText::PartialName));
    m_partial->setReadOnly(true);
    m_partial->setFrameShape(QFrame::NoFrame);
    m_partial->setBackgroundRole(QPalette::Base);
    m_partial->setPlaceholderText(transcribeText(TranscribeText::PartialPlaceholder));
    m_partial->setFixedHeight(m_partial->fontMetrics().lineSpacing() * 4
                              + int(m_partial->document()->documentMargin() * 2));
    stageLayout->addWidget(m_partial);
    settings::addCardRow(settings::cardFormLayout(m_queueCard), stage, m_queueCard);
    auto *cancel = new QPushButton(transcribeText(TranscribeText::Cancel), m_processing);
    cancel->setObjectName(QStringLiteral("transcribeCancel"));
    connect(cancel, &QPushButton::clicked, m_model, &TranscribeModel::cancel);
    m_progressTimer.setInterval(100);
    connect(&m_progressTimer, &QTimer::timeout, this, &TranscribePage::refreshProgress);
    addCentered(processing, cancel);
    column->addWidget(m_processing);

    // ---- Results ----
    m_results = new QWidget(content);
    auto *results = new QVBoxLayout(m_results);
    results->setContentsMargins(0, 0, 0, 0);
    results->setSpacing(0);
    m_resultsHeader = settings::makeSectionLabel(resultsTitle(2), m_results);
    results->addWidget(m_resultsHeader);
    QFrame *resultsCard = settings::makeSettingsCard(m_results);
    results->addWidget(resultsCard);
    auto *top = new QWidget(resultsCard);
    auto *topLayout = new QVBoxLayout(top);
    topLayout->setContentsMargins(settings::rowPadding());
    topLayout->setSpacing(settings::smallSpacing());
    auto *toolbar = new QHBoxLayout;
    m_variants = new QWidget(top);
    auto *variantLayout = new QHBoxLayout(m_variants);
    variantLayout->setContentsMargins(0, 0, 0, 0);
    variantLayout->setSpacing(settings::largeSpacing());
    auto *variants = new QButtonGroup(this);
    for (const QString &label : {transcribeText(TranscribeText::Refined), transcribeText(TranscribeText::Raw)}) {
        auto *button = new QRadioButton(label, m_variants);
        variants->addButton(button);
        variantLayout->addWidget(button);
    }
    m_showRefined = variants->buttons().first();
    connect(variants, &QButtonGroup::buttonToggled, this, [this](QAbstractButton *, bool checked) {
        if (checked) {
            showResults();
        }
    });
    toolbar->addWidget(m_variants);
    toolbar->addStretch();
    auto *copyAll = new QPushButton(transcribeText(TranscribeText::CopyAll), top);
    auto *exportAll = new QPushButton(transcribeText(TranscribeText::ExportAll), top);
    exportAll->setObjectName(QStringLiteral("transcribeExportAll"));
    toolbar->addWidget(copyAll);
    toolbar->addWidget(exportAll);
    topLayout->addLayout(toolbar);
    m_summary = dimLabel(QString(), top);
    m_summary->setObjectName(QStringLiteral("transcribeSummary"));
    m_summary->setWordWrap(true);
    topLayout->addWidget(m_summary);
    m_subtitlesNote = dimLabel(QString(), top);
    m_subtitlesNote->setWordWrap(true);
    topLayout->addWidget(m_subtitlesNote);
    m_problem = new InlineMessage(top);
    m_problem->setObjectName(QStringLiteral("transcribeProblem"));
    m_problem->setType(InlineMessage::Type::Error);
    m_problem->setCloseButtonVisible(true);
    m_problem->hide();
    topLayout->addWidget(m_problem);
    settings::addCardRow(settings::cardFormLayout(resultsCard), top, resultsCard);
    m_resultsCard = resultsCard;
    m_resultsCard->installEventFilter(this);
    connect(copyAll, &QPushButton::clicked, this, [this, copyAll] {
        QGuiApplication::clipboard()->setText(allTranscripts(m_model->results(), showingRaw()));
        copyAll->setText(transcribeText(TranscribeText::Copied));
        QTimer::singleShot(1500, copyAll, [copyAll] { copyAll->setText(transcribeText(TranscribeText::CopyAll)); });
    });
    connect(exportAll, &QPushButton::clicked, this, &TranscribePage::exportAll);
    // Closing the message clears it for every view.
    connect(m_problem->closeButton(), &QToolButton::clicked, m_model, [this] { m_model->setProblem({}); });
    auto *again = new QPushButton(transcribeText(TranscribeText::TranscribeMore), m_results);
    again->setObjectName(QStringLiteral("transcribeAgain"));
    connect(again, &QPushButton::clicked, m_model, &TranscribeModel::backToSetup);
    addCentered(results, again);
    column->addWidget(m_results);
    column->addStretch();

    connect(m_model, &TranscribeModel::filesChanged, this, &TranscribePage::refreshFileList);
    connect(m_model, &TranscribeModel::stepChanged, this, &TranscribePage::showStep);
    connect(m_model, &TranscribeModel::fileChanged, this, &TranscribePage::refreshFile);
    connect(m_model, &TranscribeModel::fileFinished, this, [this] {
        m_progressTimer.stop();
        m_loom->finishFile();
        m_percent->setText(percentLabel(1.0));
        refreshQueue();
    });
    connect(m_model, &TranscribeModel::resultsChanged, this, &TranscribePage::showResults);
    connect(m_model, &TranscribeModel::problemChanged, this, [this] {
        m_problem->setText(m_model->problem());
        m_problem->setVisible(!m_model->problem().isEmpty());
    });

    seedOptionsFromSettings();
    refreshFileList();
    showStep();
    settings::applyLabelHierarchy(this);
}

void TranscribePage::addFiles(const QStringList &paths)
{
    m_model->addFiles(paths);
}

void TranscribePage::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_setup->isVisible() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        showDropTarget(true);
    }
}

void TranscribePage::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths << url.toLocalFile();
        }
    }
    addFiles(paths);
    event->acceptProposedAction();
    showDropTarget(false);
}

void TranscribePage::dragLeaveEvent(QDragLeaveEvent *event)
{
    QWidget::dragLeaveEvent(event);
    showDropTarget(false);
}

// While files are dragged over the page, the file chooser says a drop adds
// them; the style draws no drop highlight for a whole card.
void TranscribePage::showDropTarget(bool dragging)
{
    auto *choose = m_filesCard->findChild<QPushButton *>(QStringLiteral("transcribeChooseFiles"));
    settings::setButtonRowCaption(choose, dragging ? transcribeText(TranscribeText::DropToAdd)
                                                   : chooseFilesCaption(!m_model->files().isEmpty()));
}

// Options chosen here survive leaving the page, say to change the model in
// Refinement settings; the lists and the model row follow the settings.
void TranscribePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    showChoices(m_controller->settings()->dictationSnapshot());
    refreshRefinementRows();
}

void TranscribePage::showStep()
{
    const TranscribeStep step = m_model->step();
    m_setup->setVisible(step == TranscribeStep::Configure);
    m_processing->setVisible(step == TranscribeStep::Transcribe);
    m_results->setVisible(step == TranscribeStep::Export);
    refreshSteps(step);
    // Back from results the choices start over; back from a batch that did
    // not start or was cancelled early they stay as they were.
    const bool fromResults = m_shownStep == TranscribeStep::Export;
    m_shownStep = step;
    switch (step) {
    case TranscribeStep::Configure:
        m_progressTimer.stop();
        if (fromResults) {
            seedOptionsFromSettings();
        }
        break;
    case TranscribeStep::Transcribe:
        m_loomFile = m_model->current();
        m_loom->startFile();
        m_progressTimer.start();
        refreshFile();
        break;
    case TranscribeStep::Export:
        m_progressTimer.stop();
        m_variants->setVisible(m_model->batchRefines());
        m_showRefined->setChecked(true);
        m_problem->setText(m_model->problem());
        m_problem->setVisible(!m_model->problem().isEmpty());
        showResults();
        break;
    }
}

// Done steps are checked and dimmed, the current one is bold and the ones
// still ahead are dim.
void TranscribePage::refreshSteps(TranscribeStep current)
{
    for (int i = 0; i < m_stepLabels.size(); ++i) {
        const auto step = TranscribeStep(i);
        QLabel *label = m_stepLabels.at(i);
        const bool done = i < int(current);
        label->setText(QStringLiteral("%1 %2").arg(done ? QStringLiteral("✓") : QString::number(i + 1),
                                                   transcribeStepLabel(step)));
        QFont font = label->font();
        font.setBold(step == current);
        label->setFont(font);
        label->setForegroundRole(step == current ? QPalette::WindowText : QPalette::PlaceholderText);
    }
    m_stepHint->setText(transcribeStepHint(current));
    m_stepHint->setVisible(!m_stepHint->text().isEmpty());
}

void TranscribePage::seedOptionsFromSettings()
{
    const AppSettings settings = m_controller->settings()->snapshot();
    showChoices(settings);
    settings::selectData(m_speech, settings.speech.providerId);
    m_vocabulary->setChecked(true);
    settings::selectData(m_refiner, settings.refinement.providerId);
    if (m_refiner->currentIndex() < 0) {
        m_refiner->setCurrentIndex(0);
    }
    {
        const QSignalBlocker blocker(m_profile);
        settings::selectData(m_profile, settings.refinement.defaultWritingProfile);
    }
    applyWritingProfile();
    m_destination->setCurrentIndex(0);
    m_startError->hide();
    refreshRefinementRows();
    refreshOutputRows();
    const QString summary = m_speech->currentData(Qt::ToolTipRole).toString();
    m_speechSummary->setText(summary);
    m_speechSummary->setVisible(!summary.isEmpty());
}

// Refills the lists, keeping the picks that are still offered. A pick that is
// gone takes the profile's own; both do when the profile is gone.
void TranscribePage::showChoices(const AppSettings &settings)
{
    const auto refill = [](QComboBox *combo, const QList<RowOption> &options) {
        const QSignalBlocker blocker(combo);
        const QString picked = combo->currentData().toString();
        combo->clear();
        for (const RowOption &option : options) {
            combo->addItem(option.label, option.id);
            combo->setItemData(combo->count() - 1, option.help, kSelectionHelpRole);
        }
        const int index = combo->findData(picked);
        combo->setCurrentIndex(qMax(index, 0));
        return index >= 0;
    };
    QList<RowOption> profiles = writingProfileChoices(settings.refinement.writingProfiles);
    for (RowOption &profile : profiles) {
        profile.help = writingProfileChoiceSummary(settings, profile.id);
    }
    const bool keptProfile = refill(m_profile, profiles);
    const bool keptCleanup = refill(m_cleanup, cleanupStrengths(settings.refinement.customCleanupLevels));
    const bool keptTone = refill(m_tone, writingTones(settings.refinement.customTones));
    if (!keptProfile || !keptCleanup) {
        applyProfileCleanup();
    }
    if (!keptProfile || !keptTone) {
        applyProfileTone();
    }
    for (QComboBox *combo : {m_profile, m_cleanup, m_tone}) {
        showSelectionHelp(combo);
    }
}

// A profile brings its own cleanup strength and tone, as it does for dictation.
void TranscribePage::applyWritingProfile()
{
    applyProfileCleanup();
    applyProfileTone();
}

WritingProfileSettings TranscribePage::pickedProfile(const RefinementSettings &refinement) const
{
    return writingProfileSettingsFor(refinement.writingProfiles,
                                     writingProfileFromName(m_profile->currentData().toString()));
}

void TranscribePage::applyProfileCleanup()
{
    const RefinementSettings refinement = m_controller->settings()->snapshot().refinement;
    const WritingProfileSettings profile = pickedProfile(refinement);
    // A stored strength this build does not know falls back to the middle one.
    settings::selectData(m_cleanup,
                         offeredCleanupLevel(refinedCleanupLevel(profile.cleanupStrength, profile.outputLanguage),
                                             refinement.customCleanupLevels));
}

void TranscribePage::applyProfileTone()
{
    settings::selectData(m_tone, pickedProfile(m_controller->settings()->snapshot().refinement).tone);
}

void TranscribePage::refreshRefinementRows()
{
    const QString provider = m_refiner->currentData().toString();
    const QString model = refinementModel(provider, m_controller->settings()->snapshot().refinement);
    auto *modelLabel = m_refinerModelRow->findChild<QLabel *>(QStringLiteral("rowDescription"));
    modelLabel->setText(model);
    modelLabel->setVisible(!model.isEmpty());
    setCardRowVisible(m_refinerModelRow, !model.isEmpty());
    for (QWidget *row : std::as_const(m_refinementDependents)) {
        row->setEnabled(provider != QStringLiteral("none"));
    }
}

void TranscribePage::refreshOutputRows()
{
    const auto destination = TranscriptDestination(m_destination->currentData().toInt());
    setCardRowVisible(m_folderRow, destination == TranscriptDestination::Folder);
    m_folderPath->setText(QDir::toNativeSeparators(m_folder));
    m_destinationSummary->setText(destinationHint(destination));
}

void TranscribePage::refreshFileList()
{
    const QStringList &files = m_model->files();
    clearCardRows(m_filesCard, 1);
    QFormLayout *form = settings::cardFormLayout(m_filesCard);
    for (const QString &path : files) {
        const QFileInfo info(path);
        auto *remove = new QToolButton(m_filesCard);
        remove->setIcon(themedIcon(QStringLiteral("edit-delete-remove"), QStringLiteral("list-remove")));
        remove->setText(transcribeText(TranscribeText::RemoveFile));
        remove->setToolButtonStyle(remove->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
        remove->setToolTip(transcribeText(TranscribeText::RemoveFile));
        remove->setAutoRaise(true);
        connect(remove, &QToolButton::clicked, this, [this, path] {
            // Deleting the row from inside its own button's click is not safe.
            QTimer::singleShot(0, m_model, [model = m_model, path] { model->removeFile(path); });
        });
        // The name shortens in its middle rather than wrapping: the row's own
        // title gives way to one that elides.
        QFrame *row = settings::makeRow(QString(), audioFileDetail(info.size(), m_model->durationMs(path)), remove,
                                        m_filesCard);
        row->findChild<QLabel *>(QStringLiteral("rowTitle"))->hide();
        auto *text = qobject_cast<QVBoxLayout *>(row->findChild<QWidget *>(QStringLiteral("rowLabelCell"))->layout());
        text->insertWidget(0, fileNameLabel(path, row));
        settings::addCardRow(form, row, m_filesCard);
    }
    auto *choose = m_filesCard->findChild<QPushButton *>(QStringLiteral("transcribeChooseFiles"));
    settings::setButtonRowCaption(choose, chooseFilesCaption(!files.isEmpty()));
    choose->findChild<QLabel *>(QStringLiteral("rowDescription"))->setVisible(files.isEmpty());
    m_start->setEnabled(!files.isEmpty());
    m_noFiles->setVisible(files.isEmpty());
    m_start->setText(startCaption(int(files.size())));
    settings::applyLabelHierarchy(m_filesCard);
}

TranscribeOptions TranscribePage::options() const
{
    TranscribeOptions options;
    options.speechProviderId = m_speech->currentData().toString();
    options.applyVocabulary = m_vocabulary->isChecked();
    options.refinementProviderId = m_refiner->currentData().toString();
    options.cleanupStrength = m_cleanup->currentIndex() >= 0 ? m_cleanup->currentData().toString()
                                                            : QStringLiteral("none");
    options.tone = m_tone->currentData().toString();
    options.writingProfile = m_profile->currentData().toString();
    options.destination = TranscriptDestination(m_destination->currentData().toInt());
    options.folder = m_folder;
    return options;
}

void TranscribePage::startBatch()
{
    QString error;
    if (!m_model->start(options(), &error)) {
        m_startError->setText(error);
        m_startError->show();
        return;
    }
    m_startError->hide();
}

// The current file entered a phase, was decoded or heard more words.
void TranscribePage::refreshFile()
{
    // A retry runs from the results, which show it on its row instead.
    if (m_model->step() != TranscribeStep::Transcribe) {
        return;
    }
    if (m_model->current() != m_loomFile) {
        m_loomFile = m_model->current();
        m_loom->startFile();
        m_progressTimer.start();
    }
    m_processingHeader->setText(processingTitle(m_model->batch(), m_model->current()));
    m_loom->setPeaks(m_model->peaks());
    m_phase->setText(transcribePhaseLabel(m_model->phase()));
    if (m_partial->toPlainText() != m_model->partialText()) {
        // Newest words stay in view unless the reader has scrolled back.
        QScrollBar *scroll = m_partial->verticalScrollBar();
        const bool following = scroll->value() == scroll->maximum();
        const int position = scroll->value();
        m_partial->setPlainText(m_model->partialText());
        scroll->setValue(following ? scroll->maximum() : position);
    }
    refreshProgress();
    refreshQueue();
}

void TranscribePage::refreshProgress()
{
    const qreal progress = m_model->progress();
    m_loom->setProgress(progress);
    m_percent->setText(percentLabel(progress));
}

void TranscribePage::refreshQueue()
{
    clearCardRows(m_queueCard, 1);
    const QStringList &batch = m_model->batch();
    if (batch.size() < 2) {
        return;
    }
    QFormLayout *form = settings::cardFormLayout(m_queueCard);
    const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    for (int i = 0; i < batch.size(); ++i) {
        const TranscribeQueueState state = queueState(i, m_model->current(), m_model->results());
        QIcon icon;
        switch (state) {
        case TranscribeQueueState::Waiting:
            break;
        case TranscribeQueueState::Current:
            icon = themedIcon(QStringLiteral("media-playback-start"), QStringLiteral("go-next"));
            break;
        case TranscribeQueueState::Done:
            icon = themedIcon(QStringLiteral("checkmark"), QStringLiteral("dialog-ok-apply"));
            break;
        case TranscribeQueueState::Failed:
            icon = themedIcon(QStringLiteral("dialog-error"), QStringLiteral("emblem-error"));
            break;
        }
        QHBoxLayout *layout = nullptr;
        QWidget *row = plainRow(m_queueCard, &layout);
        auto *mark = new QLabel(row);
        mark->setFixedSize(iconSize, iconSize);
        mark->setPixmap(icon.pixmap(iconSize, iconSize));
        QLabel *name = fileNameLabel(batch.at(i), row);
        if (state == TranscribeQueueState::Waiting) {
            name->setForegroundRole(QPalette::PlaceholderText);
        }
        layout->addWidget(mark);
        layout->addWidget(name, 1);
        layout->addWidget(dimLabel(queueStateLabel(state, m_phase->text()), row));
        settings::addCardRow(form, row, m_queueCard);
    }
}

bool TranscribePage::showingRaw() const
{
    return !m_variants->isHidden() && !m_showRefined->isChecked();
}

void TranscribePage::showResults()
{
    const QList<TranscribeFileResult> &batchResults = m_model->results();
    const int retrying = m_model->retrying();
    QFrame *card = m_resultsCard;
    clearCardRows(card, 1);
    QFormLayout *form = settings::cardFormLayout(card);
    const bool raw = showingRaw();
    for (int index = 0; index < batchResults.size(); ++index) {
        const TranscribeFileResult &result = batchResults.at(index);
        auto *item = new QWidget(card);
        auto *itemLayout = new QVBoxLayout(item);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(0);
        auto *expand = new QToolButton(item);
        expand->setAutoRaise(true);
        expand->setCheckable(true);
        expand->setArrowType(Qt::RightArrow);
        auto *details = new QWidget(item);
        auto *head = new QHBoxLayout(details);
        head->setContentsMargins(0, 0, 0, 0);
        head->setSpacing(settings::relatedSpacing());
        QWidget *headRow = details;
        const QString text = shownTranscript(result, raw);
        auto *meta = new ElidingLabel(resultMeta(result, m_model->durationMs(result.path), raw), Qt::ElideRight,
                                      headRow);
        if (result.failed()) {
            QPalette palette = meta->palette();
            palette.setColor(QPalette::WindowText, settings::negativeTextColor(palette));
            meta->setPalette(palette);
        } else {
            meta->setForegroundRole(QPalette::PlaceholderText);
        }
        meta->setFont(settings::smallFont(meta->font()));
        head->addWidget(meta);
        if (!result.savedPath.isEmpty()) {
            auto *savedLabel = new QLabel(transcribeText(TranscribeText::Saved), headRow);
            savedLabel->setToolTip(QDir::toNativeSeparators(result.savedPath));
            QPalette palette = savedLabel->palette();
            palette.setColor(QPalette::WindowText, settings::positiveTextColor(palette));
            savedLabel->setPalette(palette);
            head->addWidget(savedLabel);
        }
        // Stacked under the name, the actions keep to the right edge.
        head->addStretch(1);
        itemLayout->addWidget(new ResultHead(expand, fileNameLabel(result.path, item), details, item));

        auto *body = new QLabel(text, item);
        body->setWordWrap(true);
        body->setTextInteractionFlags(Qt::TextSelectableByMouse);
        QMargins bodyMargins = settings::rowPadding();
        bodyMargins.setTop(0);
        bodyMargins.setLeft(bodyMargins.left() + expand->sizeHint().width() + settings::relatedSpacing());
        // A transcript that came through with a problem on the way (refinement
        // fell back to the raw text, saving failed) says so above its text.
        if (!result.failed() && !result.error.isEmpty()) {
            auto *warning = new InlineMessage(item);
            warning->setType(InlineMessage::Type::Warning);
            warning->setText(result.error);
            auto *warningRow = new QWidget(item);
            auto *warningLayout = new QHBoxLayout(warningRow);
            warningLayout->setContentsMargins(bodyMargins.left(), 0, bodyMargins.right(), bodyMargins.bottom());
            warningLayout->addWidget(warning);
            itemLayout->addWidget(warningRow);
        }
        body->setContentsMargins(bodyMargins);
        body->setVisible(false);
        itemLayout->addWidget(body);
        connect(expand, &QToolButton::toggled, body, [expand, body](bool open) {
            expand->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
            body->setVisible(open);
        });
        expand->setEnabled(!text.isEmpty());
        expand->setChecked(form->rowCount() == 1 && !text.isEmpty());

        if (result.failed()) {
            QToolButton *retryButton = textButton(transcribeText(index == retrying ? TranscribeText::Retrying
                                                                                   : TranscribeText::Retry),
                                                  headRow);
            retryButton->setObjectName(QStringLiteral("transcribeRetry"));
            retryButton->setEnabled(retrying < 0);
            connect(retryButton, &QToolButton::clicked, m_model, [this, index] { m_model->retry(index); });
            head->addWidget(retryButton);
        } else {
            QToolButton *copy = textButton(transcribeText(TranscribeText::Copy), headRow);
            connect(copy, &QToolButton::clicked, this, [copy, text] {
                QGuiApplication::clipboard()->setText(text);
                copy->setText(transcribeText(TranscribeText::Copied));
                QTimer::singleShot(1500, copy, [copy] { copy->setText(transcribeText(TranscribeText::Copy)); });
            });
            QToolButton *exportButton = textButton(transcribeText(TranscribeText::Export), headRow);
            // The button exports text, as it always has; its arrow offers subtitles.
            exportButton->setPopupMode(QToolButton::MenuButtonPopup);
            connect(exportButton, &QToolButton::clicked, this,
                    [this, result] { exportOne(result, TranscriptFormat::Text); });
            auto *formats = new QMenu(exportButton);
            for (TranscriptFormat format : {TranscriptFormat::Srt, TranscriptFormat::WebVtt}) {
                QAction *action = formats->addAction(transcriptFormatCaption(format));
                action->setEnabled(canExportAs(result, format));
                connect(action, &QAction::triggered, this, [this, result, format] { exportOne(result, format); });
            }
            exportButton->setMenu(formats);
            head->addWidget(copy);
            head->addWidget(exportButton);
        }
        settings::addCardRow(form, item, card);
    }

    m_resultsHeader->setText(resultsTitle(int(batchResults.size())));
    m_summary->setText(m_model->summary());
    m_subtitlesNote->setText(m_model->subtitlesNote());
    m_subtitlesNote->setVisible(!m_subtitlesNote->text().isEmpty());
    applyResultsWidth();
}

void TranscribePage::applyResultsWidth()
{
    QList<ResultHead *> heads;
    for (QWidget *head : m_resultsCard->findChildren<QWidget *>(QStringLiteral("transcribeResultHead"))) {
        heads << static_cast<ResultHead *>(head);
    }
    int oneLine = 0;
    for (const ResultHead *head : heads) {
        oneLine = std::max(oneLine, head->oneLineWidth());
    }
    const bool stacked = m_resultsCard->contentsRect().width() < oneLine;
    for (ResultHead *head : heads) {
        head->setStacked(stacked);
    }
}

bool TranscribePage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_resultsCard && event->type() == QEvent::Resize) {
        applyResultsWidth();
    }
    return QWidget::eventFilter(watched, event);
}

// Every finished transcript into one folder, numbered rather than
// overwriting what is there.
void TranscribePage::exportAll()
{
    const QString folder = QFileDialog::getExistingDirectory(this, transcribeText(TranscribeText::ExportAllDialogTitle),
                                                             QDir::homePath());
    if (folder.isEmpty()) {
        return;
    }
    QStringList errors;
    for (const TranscribeFileResult &result : m_model->results()) {
        QString error;
        if (!result.failed()) {
            saveTranscript(result.path, folder, shownTranscript(result, showingRaw()), &error);
        }
        if (!error.isEmpty()) {
            errors << error;
        }
    }
    m_model->setProblem(errors.join(QLatin1Char('\n')));
}

void TranscribePage::exportOne(const TranscribeFileResult &result, TranscriptFormat format)
{
    const QFileInfo audio(result.path);
    const QString extension = transcriptFileExtension(format);
    const QString path = QFileDialog::getSaveFileName(
        this, transcribeText(TranscribeText::ExportDialogTitle),
        audio.dir().filePath(audio.completeBaseName() + QStringLiteral("-transcribed.") + extension),
        QStringLiteral("%1 (*.%2)").arg(transcriptFormatFileType(format), extension));
    if (!path.isEmpty()) {
        m_model->setProblem(writeText(path, exportedTranscript(result, format, showingRaw())));
    }
}

} // namespace speecher
