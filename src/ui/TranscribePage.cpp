#include "ui/TranscribePage.h"

#include "app/ApplicationController.h"
#include "transcribe/TranscribePresentation.h"
#include "core/SettingsStore.h"
#include "core/Target.h"
#include "providers/ProviderRegistry.h"
#include "ui/InlineMessage.h"
#include "ui/TranscribeLoomWidget.h"
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
#include <QMimeData>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace speecher {
namespace {

const QString kSaveHint = QStringLiteral("Each transcript is saved as ⟨name⟩-transcribed.txt");

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

void writeText(const QString &path, const QString &text, QString *error)
{
    QSaveFile file(path);
    const QByteArray bytes = text.toUtf8() + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = QStringLiteral("Could not save %1: %2").arg(path, file.errorString());
    }
}

} // namespace

TranscribePage::TranscribePage(ApplicationController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
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

    m_filesCard = addCard(setup, QStringLiteral("Audio files"), m_setup);
    QPushButton *choose = settings::makeButtonRow(
        QStringLiteral("Choose audio files"),
        QStringLiteral("Drop files here or click to browse · wav, mp3, m4a, flac, ogg"),
        m_filesCard);
    choose->setObjectName(QStringLiteral("transcribeChooseFiles"));
    settings::addCardRow(settings::cardFormLayout(m_filesCard), choose, m_filesCard);
    connect(choose, &QPushButton::clicked, this, [this] {
        addFiles(QFileDialog::getOpenFileNames(
            this, QStringLiteral("Choose audio files"), QDir::homePath(),
            QStringLiteral("Audio files (*.wav *.mp3 *.m4a *.mp4 *.aac *.flac *.ogg *.oga *.opus *.webm);;All files (*)")));
    });

    QFrame *speechCard = addCard(setup, QStringLiteral("Transcription"), m_setup);
    m_speech = new QComboBox(speechCard);
    for (const ProviderDescriptor &provider : m_controller->providerRegistry()->speechProviders()) {
        m_speech->addItem(provider.label, provider.id);
        m_speech->setItemData(m_speech->count() - 1, provider.summary, Qt::ToolTipRole);
    }
    QFrame *speechRow = settings::makeRow(QStringLiteral("Model"), QString(), m_speech, speechCard, nullptr, true);
    m_speechSummary = speechRow->findChild<QLabel *>(QStringLiteral("rowDescription"));
    settings::addCardRow(settings::cardFormLayout(speechCard), speechRow, speechCard);
    connect(m_speech, &QComboBox::currentIndexChanged, this, [this] {
        const QString summary = m_speech->currentData(Qt::ToolTipRole).toString();
        m_speechSummary->setText(summary);
        m_speechSummary->setVisible(!summary.isEmpty());
    });
    m_vocabulary = new QCheckBox(speechCard);
    settings::addCardRow(settings::cardFormLayout(speechCard),
                         settings::makeRow(QStringLiteral("Apply vocabulary"),
                                           QStringLiteral("Use your custom vocabulary and corrections on the result"),
                                           m_vocabulary, speechCard),
                         speechCard);

    QFrame *refineCard = addCard(setup, QStringLiteral("Refinement"), m_setup);
    QFormLayout *refineForm = settings::cardFormLayout(refineCard);
    m_refiner = new QComboBox(refineCard);
    m_refiner->addItem(QStringLiteral("None"), QStringLiteral("none"));
    for (const ProviderDescriptor &provider : m_controller->providerRegistry()->refinementProviders()) {
        m_refiner->addItem(provider.label, provider.id);
    }
    settings::addCardRow(refineForm,
                         settings::makeRow(QStringLiteral("Provider"),
                                           QStringLiteral("Clean up the raw transcripts with a language model"),
                                           m_refiner, refineCard),
                         refineCard);
    m_refinerModel = new QLabel(refineCard);
    m_refinerModelRow = settings::makeRow(QStringLiteral("Model"),
                                          QStringLiteral("Change it on the Refinement page"),
                                          m_refinerModel, refineCard);
    settings::addCardRow(refineForm, m_refinerModelRow, refineCard);

    auto *cleanup = new QWidget(refineCard);
    auto *cleanupLayout = new QHBoxLayout(cleanup);
    cleanupLayout->setContentsMargins(0, 0, 0, 0);
    cleanupLayout->setSpacing(0);
    m_cleanup = new QButtonGroup(this);
    const QList<RowOption> strengths = cleanupStrengths();
    for (int i = 0; i < strengths.size(); ++i) {
        auto *button = new QToolButton(cleanup);
        button->setText(strengths.at(i).label);
        button->setCheckable(true);
        m_cleanup->addButton(button, i);
        cleanupLayout->addWidget(button);
    }
    QFrame *cleanupRow = settings::makeRow(QStringLiteral("Cleanup"),
                                           QStringLiteral("How much the model may rewrite"),
                                           cleanup, refineCard);
    settings::addCardRow(refineForm, cleanupRow, refineCard);

    m_profile = new QComboBox(refineCard);
    for (WritingProfile profile : {WritingProfile::Work, WritingProfile::Email, WritingProfile::Personal,
                                   WritingProfile::AiCoding, WritingProfile::Other}) {
        m_profile->addItem(writingProfileLabel(profile), writingProfileName(profile));
    }
    QFrame *profileRow = settings::makeRow(QStringLiteral("Writing profile"),
                                           QStringLiteral("Sets cleanup and tone; you can still adjust them here"),
                                           m_profile, refineCard);
    settings::addCardRow(refineForm, profileRow, refineCard);
    m_tone = new QComboBox(refineCard);
    for (const RowOption &tone : writingTones()) {
        m_tone->addItem(tone.label, tone.id);
    }
    QFrame *toneRow = settings::makeRow(QStringLiteral("Tone"),
                                        QStringLiteral("Optional override on top of the profile"),
                                        m_tone, refineCard);
    settings::addCardRow(refineForm, toneRow, refineCard);
    m_refinementDependents = {cleanupRow, profileRow, toneRow};
    connect(m_refiner, &QComboBox::currentIndexChanged, this, &TranscribePage::refreshRefinementRows);
    connect(m_profile, &QComboBox::currentIndexChanged, this, &TranscribePage::applyWritingProfile);

    QFrame *outputCard = addCard(setup, QStringLiteral("Output"), m_setup);
    m_destination = new QComboBox(outputCard);
    m_destination->addItem(QStringLiteral("Next to each audio file"), int(TranscriptDestination::BesideInput));
    m_destination->addItem(QStringLiteral("One folder…"), int(TranscriptDestination::Folder));
    m_destination->addItem(QStringLiteral("Just show them here"), int(TranscriptDestination::None));
    QFrame *destinationRow = settings::makeRow(QStringLiteral("Save transcripts"), kSaveHint,
                                               m_destination, outputCard);
    m_destinationSummary = destinationRow->findChild<QLabel *>(QStringLiteral("rowDescription"));
    settings::addCardRow(settings::cardFormLayout(outputCard), destinationRow, outputCard);
    auto *changeFolder = new QPushButton(QStringLiteral("Change…"), outputCard);
    m_folderRow = settings::makeRow(QStringLiteral("Folder"), QStringLiteral(" "), changeFolder, outputCard);
    m_folderPath = m_folderRow->findChild<QLabel *>(QStringLiteral("rowDescription"));
    settings::addCardRow(settings::cardFormLayout(outputCard), m_folderRow, outputCard);
    const auto chooseFolder = [this] {
        const QString folder = QFileDialog::getExistingDirectory(
            this, QStringLiteral("Save transcripts in"), m_folder.isEmpty() ? QDir::homePath() : m_folder);
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
    m_start = new QPushButton(QStringLiteral("Transcribe"), m_setup);
    m_start->setObjectName(QStringLiteral("transcribeStart"));
    m_start->setDefault(true);
    m_start->setMinimumWidth(160);
    connect(m_start, &QPushButton::clicked, this, &TranscribePage::startBatch);
    addCentered(setup, m_start);
    column->addWidget(m_setup);

    // ---- Processing ----
    m_processing = new QWidget(content);
    auto *processing = new QVBoxLayout(m_processing);
    processing->setContentsMargins(0, 0, 0, 0);
    processing->setSpacing(0);
    m_processingHeader = settings::makeSectionLabel(QStringLiteral("Transcribing"), m_processing);
    processing->addWidget(m_processingHeader);
    m_queueCard = settings::makeSettingsCard(m_processing);
    processing->addWidget(m_queueCard);
    auto *stage = new QWidget(m_queueCard);
    auto *stageLayout = new QVBoxLayout(stage);
    stageLayout->setContentsMargins(settings::rowPadding());
    m_loom = new TranscribeLoomWidget(stage);
    stageLayout->addWidget(m_loom);
    auto *statusLine = new QHBoxLayout;
    m_phase = new QLabel(stage);
    m_percent = dimLabel(QString(), stage);
    statusLine->addWidget(m_phase);
    statusLine->addStretch();
    statusLine->addWidget(m_percent);
    stageLayout->addLayout(statusLine);
    settings::addCardRow(settings::cardFormLayout(m_queueCard), stage, m_queueCard);
    auto *cancel = new QPushButton(QStringLiteral("Cancel"), m_processing);
    cancel->setObjectName(QStringLiteral("transcribeCancel"));
    connect(cancel, &QPushButton::clicked, m_controller->fileTranscription(), &FileTranscriptionSession::cancel);
    m_progressTimer.setInterval(100);
    connect(&m_progressTimer, &QTimer::timeout, this, &TranscribePage::refreshProgress);
    connect(m_loom, &TranscribeLoomWidget::landed, this, [this] {
        while (!m_afterLanding.isEmpty() && !m_loom->isLanding()) {
            m_afterLanding.takeFirst()();
        }
    });
    addCentered(processing, cancel);
    column->addWidget(m_processing);

    // ---- Results ----
    m_results = new QWidget(content);
    auto *results = new QVBoxLayout(m_results);
    results->setContentsMargins(0, 0, 0, 0);
    results->setSpacing(0);
    m_resultsHeader = settings::makeSectionLabel(QStringLiteral("Transcripts"), m_results);
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
    variantLayout->setSpacing(0);
    auto *variants = new QButtonGroup(this);
    for (const QString &label : {QStringLiteral("Refined"), QStringLiteral("Raw")}) {
        auto *button = new QToolButton(m_variants);
        button->setText(label);
        button->setCheckable(true);
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
    auto *copyAll = new QPushButton(QStringLiteral("Copy all"), top);
    auto *exportAll = new QPushButton(QStringLiteral("Export all"), top);
    toolbar->addWidget(copyAll);
    toolbar->addWidget(exportAll);
    topLayout->addLayout(toolbar);
    m_summary = dimLabel(QString(), top);
    m_summary->setWordWrap(true);
    topLayout->addWidget(m_summary);
    settings::addCardRow(settings::cardFormLayout(resultsCard), top, resultsCard);
    m_resultsCard = resultsCard;
    connect(copyAll, &QPushButton::clicked, this, [this, copyAll] {
        QGuiApplication::clipboard()->setText(allTranscripts(m_batchResults, showingRaw()));
        copyAll->setText(QStringLiteral("Copied"));
        QTimer::singleShot(1500, copyAll, [copyAll] { copyAll->setText(QStringLiteral("Copy all")); });
    });
    connect(exportAll, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Export transcripts to"),
                                                                 QDir::homePath());
        if (folder.isEmpty()) {
            return;
        }
        QStringList errors;
        for (const TranscribeFileResult &result : std::as_const(m_batchResults)) {
            QString error;
            if (!result.failed()) {
                saveTranscript(result.path, folder, shownTranscript(result, showingRaw()), &error);
            }
            if (!error.isEmpty()) {
                errors << error;
            }
        }
        m_summary->setText(errors.isEmpty() ? m_summary->text() : errors.join(QLatin1Char('\n')));
    });
    auto *again = new QPushButton(QStringLiteral("Transcribe more files"), m_results);
    again->setObjectName(QStringLiteral("transcribeAgain"));
    connect(again, &QPushButton::clicked, this, &TranscribePage::backToSetup);
    addCentered(results, again);
    column->addWidget(m_results);
    column->addStretch();

    // A file's events wait while the loom lands the one before it; the engine
    // starts the next file the moment the last one finishes.
    FileTranscriptionSession *session = m_controller->fileTranscription();
    connect(session, &FileTranscriptionSession::fileStarted, this, [this](int index, const QString &path) {
        afterLanding([this, index, path] {
            m_current = index;
            m_currentPath = path;
            m_processingHeader->setText(processingTitle(m_batch, index));
            m_fractionSent = 0.0;
            m_loom->startFile({}, index);
            setPhase(TranscribePhase::Reading);
            m_progressTimer.start();
        });
    });
    connect(session, &FileTranscriptionSession::fileDecoded, this,
            [this](int index, const QVector<float> &peaks, qint64 durationMs) {
                afterLanding([this, index, peaks, durationMs] {
                    m_durationsMs.insert(m_currentPath, durationMs);
                    m_loom->startFile(peaks, index);
                    setPhase(TranscribePhase::Transcribing);
                });
            });
    connect(session, &FileTranscriptionSession::fileProgress, this, [this](int, qreal fraction) {
        afterLanding([this, fraction] {
            m_fractionSent = fraction;
            if (fraction >= 1.0) {
                setPhase(TranscribePhase::Finishing);
            } else {
                refreshProgress();
            }
        });
    });
    connect(session, &FileTranscriptionSession::fileRefining, this,
            [this] { afterLanding([this] { setPhase(TranscribePhase::Refining); }); });
    connect(session, &FileTranscriptionSession::fileFinished, this,
            [this](int, const TranscribeFileResult &result) {
                afterLanding([this, result] {
                    if (m_retrying >= 0) {
                        return;
                    }
                    m_progressTimer.stop();
                    m_percent->setText(QStringLiteral("100%"));
                    m_batchResults.append(result);
                    refreshQueue();
                    m_loom->finishFile();
                });
            });
    connect(session, &FileTranscriptionSession::batchFinished, this,
            [this](const QList<TranscribeFileResult> &results, bool cancelled) {
                afterLanding([this, results, cancelled] {
                    m_running = false;
                    m_progressTimer.stop();
                    m_current = -1;
                    if (m_retrying >= 0) {
                        if (!results.isEmpty()) {
                            m_batchResults[m_retrying] = results.first();
                        }
                        m_retrying = -1;
                        showResults();
                        return;
                    }
                    if (m_batchResults.isEmpty()) {
                        showStage(TranscribeStep::Configure);
                        return;
                    }
                    m_cancelled = cancelled;
                    m_variants->setVisible(refinesTranscripts(m_batchOptions));
                    m_showRefined->setChecked(true);
                    showResults();
                    showStage(TranscribeStep::Export);
                });
            });

    seedOptionsFromSettings();
    refreshFileList();
    showStage(TranscribeStep::Configure);
    settings::applyLabelHierarchy(this);
}

void TranscribePage::addFiles(const QStringList &paths)
{
    // Files opened over finished results start the next batch; files opened
    // while one runs wait in the setup list it returns to.
    if (m_results->isVisibleTo(this)) {
        backToSetup();
    }
    for (const QString &path : paths) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (isAudioFile(absolute) && !m_files.contains(absolute)) {
            m_files << absolute;
            probeAudioDuration(absolute, this, [this, absolute](qint64 durationMs) {
                m_durationsMs.insert(absolute, durationMs);
                refreshFileList();
            });
        }
    }
    refreshFileList();
}

// The finished files leave the list; files added meanwhile, and any a
// cancelled batch never reached, stay.
void TranscribePage::backToSetup()
{
    for (const TranscribeFileResult &result : std::as_const(m_batchResults)) {
        m_files.removeOne(result.path);
    }
    refreshFileList();
    seedOptionsFromSettings();
    showStage(TranscribeStep::Configure);
}

void TranscribePage::dragEnterEvent(QDragEnterEvent *event)
{
    if (m_setup->isVisible() && event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
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
}

void TranscribePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    if (m_setup->isVisible()) {
        seedOptionsFromSettings();
    }
}

void TranscribePage::showStage(TranscribeStep step)
{
    m_setup->setVisible(step == TranscribeStep::Configure);
    m_processing->setVisible(step == TranscribeStep::Transcribe);
    m_results->setVisible(step == TranscribeStep::Export);
    refreshSteps(step);
}

// Done steps are checked in the positive colour, the current one is bold and
// the ones still ahead are dim.
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
        QPalette palette = this->palette();
        if (done) {
            palette.setColor(QPalette::WindowText, settings::positiveTextColor(palette));
        }
        label->setPalette(palette);
        label->setForegroundRole(i > int(current) ? QPalette::PlaceholderText : QPalette::WindowText);
    }
    m_stepHint->setText(transcribeStepHint(current));
    m_stepHint->setVisible(!m_stepHint->text().isEmpty());
}

void TranscribePage::seedOptionsFromSettings()
{
    const AppSettings settings = m_controller->settings()->snapshot();
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

// A profile brings its own cleanup strength and tone, as it does for dictation.
void TranscribePage::applyWritingProfile()
{
    const WritingProfileSettings profile = writingProfileSettingsFor(
        m_controller->settings()->snapshot().refinement.writingProfiles,
        writingProfileFromName(m_profile->currentData().toString()));
    const QList<RowOption> strengths = cleanupStrengths();
    const auto indexOf = [&strengths](const QString &id) {
        return int(std::find_if(strengths.cbegin(), strengths.cend(),
                                [&id](const RowOption &option) { return option.id == id; })
                   - strengths.cbegin());
    };
    // A stored strength this build does not know falls back to the middle one.
    int checked = indexOf(profile.cleanupStrength);
    if (checked == strengths.size()) {
        checked = indexOf(QStringLiteral("balanced"));
    }
    if (QAbstractButton *button = m_cleanup->button(checked)) {
        button->setChecked(true);
    }
    settings::selectData(m_tone, profile.tone);
}

void TranscribePage::refreshRefinementRows()
{
    const QString provider = m_refiner->currentData().toString();
    const QString model = refinementModel(provider, m_controller->settings()->snapshot().refinement);
    m_refinerModel->setText(model);
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
    m_destinationSummary->setText(destination == TranscriptDestination::None
                                      ? QStringLiteral("Copy or export from the results afterwards")
                                      : kSaveHint);
}

void TranscribePage::refreshFileList()
{
    clearCardRows(m_filesCard, 1);
    QFormLayout *form = settings::cardFormLayout(m_filesCard);
    for (const QString &path : std::as_const(m_files)) {
        const QFileInfo info(path);
        auto *remove = new QToolButton(m_filesCard);
        remove->setIcon(themedIcon(QStringLiteral("edit-delete-remove"), QStringLiteral("list-remove")));
        remove->setText(QStringLiteral("Remove"));
        remove->setToolButtonStyle(remove->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonIconOnly);
        remove->setToolTip(QStringLiteral("Remove"));
        remove->setAutoRaise(true);
        connect(remove, &QToolButton::clicked, this, [this, path] {
            m_files.removeOne(path);
            // Deleting the row from inside its own button's click is not safe.
            QTimer::singleShot(0, this, &TranscribePage::refreshFileList);
        });
        settings::addCardRow(form,
                             settings::makeRow(info.fileName(),
                                               audioFileDetail(info.size(), m_durationsMs.value(path, -1)),
                                               remove, m_filesCard),
                             m_filesCard);
    }
    auto *choose = m_filesCard->findChild<QPushButton *>(QStringLiteral("transcribeChooseFiles"));
    settings::setButtonRowCaption(choose, m_files.isEmpty() ? QStringLiteral("Choose audio files")
                                                            : QStringLiteral("Add more files"));
    choose->findChild<QLabel *>(QStringLiteral("rowDescription"))->setVisible(m_files.isEmpty());
    m_start->setEnabled(!m_files.isEmpty());
    m_start->setText(m_files.size() > 1 ? QStringLiteral("Transcribe %1 files").arg(m_files.size())
                                        : QStringLiteral("Transcribe"));
    settings::applyLabelHierarchy(m_filesCard);
}

TranscribeOptions TranscribePage::options() const
{
    TranscribeOptions options;
    options.speechProviderId = m_speech->currentData().toString();
    options.applyVocabulary = m_vocabulary->isChecked();
    options.refinementProviderId = m_refiner->currentData().toString();
    options.cleanupStrength = m_cleanup->checkedId() >= 0 ? cleanupStrengths().value(m_cleanup->checkedId()).id
                                                          : QStringLiteral("none");
    options.tone = m_tone->currentData().toString();
    options.writingProfile = m_profile->currentData().toString();
    options.destination = TranscriptDestination(m_destination->currentData().toInt());
    options.folder = m_folder;
    return options;
}

void TranscribePage::startBatch()
{
    m_batch = m_files;
    m_batchOptions = options();
    m_batchLabels = batchLabels(m_batchOptions, *m_controller->providerRegistry(),
                                m_controller->settings()->snapshot().refinement);
    m_batchResults.clear();
    m_cancelled = false;
    // Before start(): a batch whose files all fail at once finishes inside it.
    showStage(TranscribeStep::Transcribe);
    m_running = true;
    QString error;
    if (!m_controller->startFileTranscription(m_batch, m_batchOptions, &error)) {
        m_running = false;
        m_startError->setText(error);
        m_startError->show();
        showStage(TranscribeStep::Configure);
        return;
    }
    m_startError->hide();
}

// Runs one failed file again with the batch's choices; its row takes the new
// result.
void TranscribePage::retry(int index)
{
    m_retrying = index;
    m_running = true;
    QString error;
    if (!m_controller->startFileTranscription({m_batchResults.at(index).path}, m_batchOptions, &error)) {
        m_retrying = -1;
        m_running = false;
        m_summary->setText(error);
        return;
    }
    showResults();
}

void TranscribePage::setPhase(TranscribePhase phase)
{
    m_phaseNow = phase;
    m_phaseClock.start();
    m_phase->setText(transcribePhaseLabel(phase));
    refreshProgress();
    refreshQueue();
}

void TranscribePage::refreshProgress()
{
    const qreal progress = overallFileProgress(m_fractionSent, m_phaseNow, refinesTranscripts(m_batchOptions),
                                               m_phaseClock.elapsed());
    m_loom->setProgress(progress);
    m_percent->setText(QStringLiteral("%1%").arg(int(progress * 100)));
}

void TranscribePage::afterLanding(std::function<void()> event)
{
    // Another surface's batch shares the engine; this page shows only its own.
    if (!m_running) {
        return;
    }
    if (m_loom->isLanding()) {
        m_afterLanding << std::move(event);
        return;
    }
    event();
}

void TranscribePage::refreshQueue()
{
    clearCardRows(m_queueCard, 1);
    if (m_batch.size() < 2) {
        return;
    }
    QFormLayout *form = settings::cardFormLayout(m_queueCard);
    const int iconSize = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
    for (int i = 0; i < m_batch.size(); ++i) {
        const TranscribeQueueState state = queueState(i, m_current, m_batchResults);
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
        auto *name = new QLabel(QFileInfo(m_batch.at(i)).fileName(), row);
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
    QFrame *card = m_resultsCard;
    clearCardRows(card, 1);
    QFormLayout *form = settings::cardFormLayout(card);
    const bool raw = showingRaw();
    for (int index = 0; index < m_batchResults.size(); ++index) {
        const TranscribeFileResult &result = m_batchResults.at(index);
        auto *item = new QWidget(card);
        auto *itemLayout = new QVBoxLayout(item);
        itemLayout->setContentsMargins(0, 0, 0, 0);
        itemLayout->setSpacing(0);
        QHBoxLayout *head = nullptr;
        QWidget *headRow = plainRow(item, &head);
        auto *expand = new QToolButton(headRow);
        expand->setAutoRaise(true);
        expand->setCheckable(true);
        expand->setArrowType(Qt::RightArrow);
        head->addWidget(expand);
        auto *name = new QLabel(QFileInfo(result.path).fileName(), headRow);
        head->addWidget(name);
        const QString text = shownTranscript(result, raw);
        QLabel *metaLabel = dimLabel(resultMeta(result, m_durationsMs.value(result.path, -1), raw), headRow);
        head->addWidget(metaLabel, 1);
        if (!result.savedPath.isEmpty()) {
            auto *savedLabel = new QLabel(QStringLiteral("Saved"), headRow);
            savedLabel->setToolTip(QDir::toNativeSeparators(result.savedPath));
            QPalette palette = savedLabel->palette();
            palette.setColor(QPalette::WindowText, settings::positiveTextColor(palette));
            savedLabel->setPalette(palette);
            head->addWidget(savedLabel);
        }
        itemLayout->addWidget(headRow);

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
            QToolButton *retryButton = textButton(index == m_retrying ? QStringLiteral("Retrying\u2026")
                                                                      : QStringLiteral("Retry"),
                                                  headRow);
            retryButton->setObjectName(QStringLiteral("transcribeRetry"));
            retryButton->setEnabled(m_retrying < 0);
            connect(retryButton, &QToolButton::clicked, this, [this, index] { retry(index); });
            head->addWidget(retryButton);
        } else {
            QToolButton *copy = textButton(QStringLiteral("Copy"), headRow);
            connect(copy, &QToolButton::clicked, this, [copy, text] {
                QGuiApplication::clipboard()->setText(text);
                copy->setText(QStringLiteral("Copied"));
                QTimer::singleShot(1500, copy, [copy] { copy->setText(QStringLiteral("Copy")); });
            });
            QToolButton *exportButton = textButton(QStringLiteral("Export"), headRow);
            const QString audioPath = result.path;
            connect(exportButton, &QToolButton::clicked, this, [this, audioPath, text, metaLabel] {
                const QFileInfo audio(audioPath);
                const QString path = QFileDialog::getSaveFileName(
                    this, QStringLiteral("Export transcript"),
                    audio.dir().filePath(audio.completeBaseName() + QStringLiteral("-transcribed.txt")),
                    QStringLiteral("Text files (*.txt)"));
                if (path.isEmpty()) {
                    return;
                }
                QString error;
                writeText(path, text, &error);
                if (!error.isEmpty()) {
                    metaLabel->setText(error);
                }
            });
            head->addWidget(copy);
            head->addWidget(exportButton);
        }
        settings::addCardRow(form, item, card);
    }

    m_resultsHeader->setText(m_batchResults.size() > 1 ? QStringLiteral("Transcripts") : QStringLiteral("Transcript"));
    m_summary->setText(batchSummary(m_batchResults, int(m_batch.size()), m_cancelled, m_durationsMs,
                                    m_batchOptions, m_batchLabels));
}

} // namespace speecher
