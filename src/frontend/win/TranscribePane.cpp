#include "frontend/win/TranscribePane.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "core/Target.h"
#include "core/settings/SettingsSchema.h"
#include "frontend/win/SettingsPage.h"
#include "providers/ProviderRegistry.h"
#include "transcribe/TranscribePresentation.h"

#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>

#include <shobjidl.h>

#pragma push_macro("GetCurrentTime")
#undef GetCurrentTime
#include <winrt/Windows.ApplicationModel.DataTransfer.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Microsoft.UI.h>
#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#pragma pop_macro("GetCurrentTime")

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation;
using winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats;
namespace Pickers = winrt::Windows::Storage::Pickers;

// Segoe Fluent Icons.
constexpr wchar_t kGlyphRemove = L'\uE711';
constexpr wchar_t kGlyphChevron = L'\uE76C';
constexpr wchar_t kGlyphDone = L'\uE73E';
constexpr wchar_t kGlyphFailed = L'\uE783';
constexpr wchar_t kGlyphCurrent = L'\uE768';
// How often the progress eases forward through the open-ended waits.
constexpr int kProgressIntervalMs = 100;
// The live transcript shows about four lines, newest words in view.
constexpr double kPartialHeight = 80;

QString writeText(const QString &path, const QString &text)
{
    QSaveFile file(path);
    const QByteArray bytes = text.toUtf8() + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return transcriptSaveError(path, file.errorString());
    }
    return {};
}

// A SelectorBar over (id, label) pairs that reports the chosen id, as the
// other front ends show a cleanup level: every choice in view.
SelectorBar selectorBar(const QList<RowOption> &options,
                        const QString &current,
                        std::function<void(const QString &)> chosen)
{
    SelectorBar bar;
    for (const RowOption &option : options) {
        SelectorBarItem item;
        item.Text(hs(option.label));
        item.Tag(box_value(hs(option.id)));
        bar.Items().Append(item);
        if (option.id == current) {
            bar.SelectedItem(item);
        }
    }
    bar.SelectionChanged([chosen = std::move(chosen)](const SelectorBar &sender, const auto &) {
        if (const auto item = sender.SelectedItem()) {
            chosen(qs(unbox_value<hstring>(item.Tag())));
        }
    });
    return bar;
}

// A ComboBox over (id, label) pairs that reports the chosen id.
ComboBox comboBox(const QList<RowOption> &options,
                  const QString &current,
                  std::function<void(const QString &)> chosen)
{
    ComboBox combo;
    combo.MinWidth(120);
    int selected = -1;
    for (const RowOption &option : options) {
        ComboBoxItem item;
        item.Content(box_value(hs(option.label)));
        item.Tag(box_value(hs(option.id)));
        if (option.id == current) {
            selected = combo.Items().Size();
        }
        combo.Items().Append(item);
    }
    combo.SelectedIndex(selected);
    combo.SelectionChanged([chosen = std::move(chosen)](const IInspectable &sender, const auto &) {
        if (const auto item = sender.as<ComboBox>().SelectedItem()) {
            chosen(qs(unbox_value<hstring>(item.as<ComboBoxItem>().Tag())));
        }
    });
    return combo;
}

Button textButton(const QString &text)
{
    Button button;
    button.Content(box_value(hs(text)));
    return button;
}

FontIcon glyph(wchar_t code)
{
    FontIcon icon;
    icon.Glyph(hstring(std::wstring_view(&code, 1)));
    icon.FontSize(16);
    return icon;
}

void addColumn(const Grid &grid, GridLength width)
{
    ColumnDefinition column;
    column.Width(width);
    grid.ColumnDefinitions().Append(column);
}

// Two columns: what grows on the left, what sizes itself on the right.
Grid lineGrid()
{
    Grid grid;
    grid.ColumnSpacing(12);
    addColumn(grid, {1, GridUnitType::Star});
    addColumn(grid, {0, GridUnitType::Auto});
    return grid;
}

// "1 Configure · 2 Transcribe · 3 Export" under the title, left-aligned with
// it: the current step in bold, the steps behind it checked and dim, the ones
// ahead dim, and a hint under Configure that it is only a check before
// pressing Transcribe.
void appendSteps(const StackPanel &column, TranscribeStep current, const PaneHost &host)
{
    StackPanel steps;
    steps.Orientation(Orientation::Horizontal);
    steps.Spacing(8);
    steps.Margin({0, 16, 0, 0});
    for (TranscribeStep step : {TranscribeStep::Configure, TranscribeStep::Transcribe, TranscribeStep::Export}) {
        const int number = int(step) + 1;
        if (number > 1) {
            steps.Children().Append(secondaryTextBlock(QStringLiteral("\u00b7"), L"SettingsCardBodyStyle", host));
        }
        const QString label = transcribeStepLabel(step);
        if (step < current) {
            StackPanel done;
            done.Orientation(Orientation::Horizontal);
            done.Spacing(4);
            FontIcon check = glyph(kGlyphDone);
            check.FontSize(14);
            check.VerticalAlignment(VerticalAlignment::Center);
            if (const auto dim = themeBrush(L"SettingsCardDescriptionForeground", host)) {
                check.Foreground(dim);
            }
            done.Children().Append(check);
            done.Children().Append(secondaryTextBlock(label, L"SettingsCardBodyStyle", host));
            steps.Children().Append(done);
        } else if (step == current) {
            steps.Children().Append(
                styledTextBlock(QStringLiteral("%1 %2").arg(number).arg(label), L"BodyStrongTextBlockStyle"));
        } else {
            steps.Children().Append(
                secondaryTextBlock(QStringLiteral("%1 %2").arg(number).arg(label), L"SettingsCardBodyStyle", host));
        }
    }
    column.Children().Append(steps);
    if (current == TranscribeStep::Configure) {
        TextBlock hint = secondaryTextBlock(transcribeStepHint(TranscribeStep::Configure), L"SettingsCardDescriptionStyle", host);
        hint.Margin({0, 4, 0, 0});
        column.Children().Append(hint);
    }
}

} // namespace

TranscribePane::TranscribePane(ApplicationController *controller)
    : m_controller(controller)
{
    m_progressTimer.setInterval(kProgressIntervalMs);
    connect(&m_progressTimer, &QTimer::timeout, this, &TranscribePane::showProgress);

    // Every event goes through afterLanding: a finished file holds at 100% for
    // a moment, and the session has already started the next one.
    FileTranscriptionSession *session = m_controller->fileTranscription();
    connect(session, &FileTranscriptionSession::fileStarted, this, [this](int index, const QString &path) {
        afterLanding([this, index, path] {
            m_current = index;
            m_currentPath = path;
            m_fractionSent = 0;
            setPartialText({});
            m_progress = {};
            m_fileFinished = false;
            for (const View &view : m_views) {
                if (view.headerText) {
                    view.headerText.Text(hs(processingTitle(m_batch, m_current)));
                }
            }
            setPhase(TranscribePhase::Reading);
        });
    });
    connect(session, &FileTranscriptionSession::fileDecoded, this,
            [this](int, const QVector<float> &, qint64 durationMs) {
                afterLanding([this, durationMs] {
                    m_durationsMs.insert(m_currentPath, durationMs);
                    setPhase(TranscribePhase::Transcribing);
                });
            });
    connect(session, &FileTranscriptionSession::fileProgress, this, [this](int, qreal fraction) {
        afterLanding([this, fraction] {
            m_fractionSent = fraction;
            if (fraction >= 1.0) {
                setPhase(TranscribePhase::Finishing);
            } else {
                showProgress();
            }
        });
    });
    connect(session, &FileTranscriptionSession::filePartialText, this, [this](int, const QString &text) {
        afterLanding([this, text] { setPartialText(text); });
    });
    connect(session, &FileTranscriptionSession::fileRefining, this,
            [this] { afterLanding([this] { setPhase(TranscribePhase::Refining); }); });
    connect(session, &FileTranscriptionSession::fileFinished, this,
            [this](int, const TranscribeFileResult &result) {
                afterLanding([this, result] {
                    if (m_retrying >= 0) {
                        return;
                    }
                    m_results.append(result);
                    m_fileFinished = true;
                    showProgress();
                    refreshQueue();
                    m_landing = true;
                    QTimer::singleShot(kTranscribeLandingMs, this, &TranscribePane::land);
                });
            });
    connect(session, &FileTranscriptionSession::batchFinished, this,
            [this](const QList<TranscribeFileResult> &results, bool cancelled) {
                afterLanding([this, results, cancelled] {
                    m_current = -1;
                    if (m_retrying >= 0) {
                        if (!results.isEmpty()) {
                            m_results[m_retrying] = results.first();
                        }
                        m_retrying = -1;
                        rebuild();
                        return;
                    }
                    m_progressTimer.stop();
                    m_cancelled = cancelled;
                    // A batch cancelled before any file finished has nothing to show.
                    m_step = m_results.isEmpty() ? TranscribeStep::Configure : TranscribeStep::Export;
                    m_showRaw = false;
                    m_expanded.clear();
                    if (!m_results.isEmpty() && !m_results.first().refined.isEmpty()) {
                        m_expanded.insert(0);
                    }
                    m_resultsProblem.clear();
                    rebuild();
                });
            });
    seedOptions();
}

void TranscribePane::addFiles(const QStringList &paths)
{
    // Files opened over finished results start the next batch.
    if (m_step == TranscribeStep::Export) {
        backToSetup();
    }
    for (const QString &path : paths) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (isAudioFile(absolute) && !m_files.contains(absolute)) {
            m_files << absolute;
            probeAudioDuration(absolute, this, [this, absolute](qint64 durationMs) {
                m_durationsMs.insert(absolute, durationMs);
                if (m_step == TranscribeStep::Configure) {
                    rebuild();
                }
            });
        }
    }
    rebuild();
}

// The finished files leave the list; files added while the batch ran, and any
// a cancelled batch never reached, stay.
void TranscribePane::backToSetup()
{
    for (const TranscribeFileResult &result : std::as_const(m_results)) {
        m_files.removeOne(result.path);
    }
    seedOptions();
    m_step = TranscribeStep::Configure;
}

void TranscribePane::enter()
{
    // Another window showing the pane keeps the choices made there.
    if (m_step == TranscribeStep::Configure && m_views.empty()) {
        seedOptions();
    }
}

void TranscribePane::forget(const PaneHost &host)
{
    std::erase_if(m_views, [&host](const View &view) { return view.host == &host; });
}

// Re-renders the pane in every window showing it.
void TranscribePane::rebuild()
{
    for (const View &view : m_views) {
        if (view.host->refresh) {
            view.host->refresh();
        }
    }
}

void TranscribePane::seedOptions()
{
    const AppSettings settings = m_controller->settings()->snapshot();
    m_speech = settings.speech.providerId;
    m_vocabulary = true;
    m_refiner = settings.refinement.providerId;
    m_profile = settings.refinement.defaultWritingProfile;
    applyWritingProfile();
    m_destination = TranscriptDestination::BesideInput;
    m_startError.clear();
}

// A profile brings its own cleanup strength and tone, as it does for dictation.
void TranscribePane::applyWritingProfile()
{
    const RefinementSettings refinement = m_controller->settings()->snapshot().refinement;
    const WritingProfileSettings profile =
        writingProfileSettingsFor(refinement.writingProfiles, writingProfileFromName(m_profile));
    // A stored strength this build does not know falls back to the middle one.
    m_cleanup = offeredCleanupLevel(profile.cleanupStrength, refinement.customCleanupLevels);
    m_tone = profile.tone;
}

TranscribeOptions TranscribePane::options() const
{
    TranscribeOptions options;
    options.speechProviderId = m_speech;
    options.applyVocabulary = m_vocabulary;
    options.refinementProviderId = m_refiner;
    options.cleanupStrength = m_cleanup;
    options.tone = m_tone;
    options.writingProfile = m_profile;
    options.destination = m_destination;
    options.folder = m_folder;
    return options;
}

void TranscribePane::startBatch()
{
    m_batch = m_files;
    m_batchOptions = options();
    m_batchLabels = batchLabels(m_batchOptions, *m_controller->providerRegistry(),
                                m_controller->settings()->snapshot().refinement);
    m_results.clear();
    m_cancelled = false;
    m_current = -1;
    m_phase = TranscribePhase::Reading;
    m_phaseClock.start();
    m_fractionSent = 0;
    m_fileFinished = false;
    m_startError.clear();
    // Before start(): a batch whose files all fail at once finishes inside it.
    m_step = TranscribeStep::Transcribe;
    if (!m_controller->startFileTranscription(m_batch, m_batchOptions, &m_startError)) {
        m_step = TranscribeStep::Configure;
    }
    rebuild();
}

// Runs one failed file again with the batch's choices; its row takes the new
// result.
void TranscribePane::retry(int index)
{
    m_retrying = index;
    QString error;
    if (!m_controller->startFileTranscription({m_results.at(index).path}, m_batchOptions, &error)) {
        m_retrying = -1;
        m_resultsProblem = error;
    }
    rebuild();
}

UIElement TranscribePane::build(PaneHost &host, const QString &title)
{
    forget(host);
    m_views.push_back({&host});
    StackPanel column;
    ScrollViewer scroll = pageScaffold(title, column);
    appendSteps(column, m_step, host);
    switch (m_step) {
    case TranscribeStep::Configure:
        appendSetup(column, host);
        // Files dropped anywhere on the pane join the list. A transparent
        // background makes the whole scroller hit-testable, gutters included.
        scroll.Background(winrt::Microsoft::UI::Xaml::Media::SolidColorBrush(
            winrt::Microsoft::UI::Colors::Transparent()));
        scroll.AllowDrop(true);
        scroll.DragOver([](const IInspectable &, const DragEventArgs &args) {
            if (args.DataView().Contains(StandardDataFormats::StorageItems())) {
                args.AcceptedOperation(DataPackageOperation::Copy);
                args.DragUIOverride().Caption(hs(transcribeText(TranscribeText::DropToAdd)));
            }
        });
        scroll.Drop([this, &host](const IInspectable &, const DragEventArgs &args) { dropFiles(host, args); });
        return pageWithActionBar(scroll, startAction(host));
    case TranscribeStep::Transcribe:
        appendProcessing(column, m_views.back());
        break;
    case TranscribeStep::Export:
        appendResults(column, host);
        break;
    }
    return scroll;
}

void TranscribePane::appendSetup(const StackPanel &column, PaneHost &host)
{
    const auto row = [&host](const QString &label, const QString &help, const UIElement &control,
                             bool follows, bool enabled = true) {
        RowSnapshot snapshot;
        snapshot.label = label;
        snapshot.help = help;
        // rowGrid shows disabledHelp in place of help on a disabled row; only
        // the refinement rows are ever disabled.
        snapshot.disabledHelp = transcribeText(TranscribeText::NeedsRefiner);
        snapshot.enabled = enabled;
        return rowGrid(snapshot, control, host, follows);
    };
    const auto card = [&column](const QString &title, const StackPanel &rows) {
        column.Children().Append(styledTextBlock(title, L"SettingsSectionHeaderStyle"));
        column.Children().Append(cardContainer(rows));
    };

    // Audio files: a browse row, then one row per file with its size. The
    // browse button says what it does; the formats it takes are its tooltip.
    StackPanel files;
    Button browse = textButton(chooseFilesCaption(!m_files.isEmpty()));
    ToolTipService::SetToolTip(browse, box_value(hs(mediaFilesTooltip())));
    browse.Click([this, &host](const auto &, const auto &) { chooseFiles(host); });
    files.Children().Append(row(m_files.isEmpty() ? mediaFilesHint() : QString(), QString(), browse, false));
    for (const QString &path : std::as_const(m_files)) {
        const QFileInfo info(path);
        Button remove;
        remove.Content(glyph(kGlyphRemove));
        ToolTipService::SetToolTip(remove, box_value(hs(transcribeText(TranscribeText::RemoveFile))));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(remove, hs(transcribeText(TranscribeText::RemoveFile)));
        remove.Click([this, path](const auto &, const auto &) {
            m_files.removeOne(path);
            rebuild();
        });
        files.Children().Append(
            row(info.fileName(), audioFileDetail(info.size(), m_durationsMs.value(path, -1)), remove, true));
    }
    card(transcribeText(TranscribeText::AudioFilesSection), files);

    // Transcription.
    ProviderRegistry *registry = m_controller->providerRegistry();
    QList<RowOption> speechOptions;
    QString speechSummary;
    for (const ProviderDescriptor &provider : registry->speechProviders()) {
        speechOptions.append({provider.id, provider.label, provider.summary});
        if (provider.id == m_speech) {
            speechSummary = provider.summary;
        }
    }
    StackPanel speech;
    speech.Children().Append(row(transcribeText(TranscribeText::Service), speechSummary,
                                 comboBox(speechOptions, m_speech,
                                          [this](const QString &id) {
                                              if (id != m_speech) {
                                                  m_speech = id;
                                                  rebuild();
                                              }
                                          }),
                                 false));
    ToggleSwitch vocabulary;
    vocabulary.IsOn(m_vocabulary);
    vocabulary.Toggled([this](const IInspectable &sender, const auto &) {
        m_vocabulary = sender.as<ToggleSwitch>().IsOn();
    });
    speech.Children().Append(row(transcribeText(TranscribeText::Vocabulary),
                                 transcribeText(TranscribeText::VocabularyHelp),
                                 stateToggle(vocabulary), true));
    card(transcribeText(TranscribeText::TranscriptionSection), speech);

    // Refinement: the model is read-only here; cleanup, profile and tone only
    // matter with a provider chosen.
    QList<RowOption> refinerOptions{{QStringLiteral("none"), transcribeText(TranscribeText::NoRefiner)}};
    for (const ProviderDescriptor &provider : registry->refinementProviders()) {
        refinerOptions.append({provider.id, provider.label});
    }
    const QString model = refinementModel(m_refiner, m_controller->settings()->snapshot().refinement);
    const bool refining = m_refiner != QStringLiteral("none");
    StackPanel refine;
    refine.Children().Append(row(transcribeText(TranscribeText::Refiner),
                                 transcribeText(TranscribeText::RefinerHelp),
                                 comboBox(refinerOptions, m_refiner,
                                          [this](const QString &id) {
                                              if (id != m_refiner) {
                                                  m_refiner = id;
                                                  rebuild();
                                              }
                                          }),
                                 false));
    // The model is set in Refinement settings, which the row opens; then the
    // Writing Profile before the Cleanup Level and Tone it sets.
    if (!model.isEmpty()) {
        Button openModel;
        StackPanel modelValue;
        modelValue.Orientation(Orientation::Horizontal);
        modelValue.Spacing(8);
        modelValue.Children().Append(secondaryTextBlock(model, L"SettingsInfoTextStyle", host));
        FontIcon chevron = glyph(kGlyphChevron);
        chevron.FontSize(12);
        chevron.VerticalAlignment(VerticalAlignment::Center);
        modelValue.Children().Append(chevron);
        openModel.Content(modelValue);
        openModel.Click([&host](const auto &, const auto &) {
            if (host.showPage) {
                host.showPage(QStringLiteral("refinement"));
            }
        });
        refine.Children().Append(row(transcribeText(TranscribeText::RefinerModel),
                                     refinementModelHint(), openModel, true));
    }
    const RefinementSettings refinement = m_controller->settings()->snapshot().refinement;
    const QList<RowOption> profiles = writingProfileChoices(refinement.writingProfiles);
    refine.Children().Append(row(transcribeText(TranscribeText::WritingProfile),
                                 transcribeText(TranscribeText::WritingProfileHelp),
                                 comboBox(profiles, m_profile,
                                          [this](const QString &id) {
                                              if (id != m_profile) {
                                                  m_profile = id;
                                                  applyWritingProfile();
                                                  rebuild();
                                              }
                                          }),
                                 true, refining));
    refine.Children().Append(row(transcribeText(TranscribeText::Cleanup),
                                 transcribeText(TranscribeText::CleanupHelp),
                                 selectorBar(cleanupStrengths(refinement.customCleanupLevels), m_cleanup,
                                             [this](const QString &id) { m_cleanup = id; }),
                                 true, refining));
    refine.Children().Append(row(transcribeText(TranscribeText::Tone),
                                 transcribeText(TranscribeText::ToneHelp),
                                 comboBox(writingTones(refinement.customTones), m_tone,
                                          [this](const QString &id) { m_tone = id; }),
                                 true, refining));
    card(transcribeText(TranscribeText::RefinementSection), refine);

    // Output.
    QList<RowOption> destinations;
    for (TranscriptDestination destination :
         {TranscriptDestination::BesideInput, TranscriptDestination::Folder, TranscriptDestination::None}) {
        destinations.append({QString::number(int(destination)), destinationLabel(destination)});
    }
    StackPanel output;
    output.Children().Append(row(transcribeText(TranscribeText::SaveTranscripts),
                                 destinationHint(m_destination),
                                 comboBox(destinations, QString::number(int(m_destination)),
                                          [this, &host](const QString &id) {
                                              const auto destination = TranscriptDestination(id.toInt());
                                              if (destination == m_destination) {
                                                  return;
                                              }
                                              m_destination = destination;
                                              if (destination == TranscriptDestination::Folder
                                                  && m_folder.isEmpty()) {
                                                  chooseFolder(host);
                                              }
                                              rebuild();
                                          }),
                                 false));
    if (m_destination == TranscriptDestination::Folder) {
        Button change = textButton(transcribeText(TranscribeText::ChangeFolder));
        change.Click([this, &host](const auto &, const auto &) { chooseFolder(host); });
        output.Children().Append(row(transcribeText(TranscribeText::Folder), QDir::toNativeSeparators(m_folder), change, true));
    }
    card(transcribeText(TranscribeText::OutputSection), output);

    if (!m_startError.isEmpty()) {
        InfoBar problem;
        problem.IsClosable(false);
        problem.IsOpen(true);
        problem.Severity(InfoBarSeverity::Error);
        problem.Message(hs(m_startError));
        problem.Margin({0, 16, 0, 0});
        column.Children().Append(problem);
    }
}

// The page's one action, kept in view under the column, with why it cannot
// start yet beside it.
StackPanel TranscribePane::startAction(const PaneHost &host)
{
    StackPanel action;
    action.Orientation(Orientation::Horizontal);
    action.Spacing(12);
    action.HorizontalAlignment(HorizontalAlignment::Right);
    if (m_files.isEmpty()) {
        TextBlock reason = secondaryTextBlock(transcribeText(TranscribeText::NoFilesYet),
                                              L"SettingsCardDescriptionStyle", host);
        reason.VerticalAlignment(VerticalAlignment::Center);
        action.Children().Append(reason);
    }
    Button start = textButton(startCaption(int(m_files.size())));
    start.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
    start.MinWidth(160);
    start.IsEnabled(!m_files.isEmpty());
    start.Click([this](const auto &, const auto &) { startBatch(); });
    action.Children().Append(start);
    return action;
}

void TranscribePane::appendProcessing(const StackPanel &column, View &view)
{
    view.headerText = styledTextBlock(processingTitle(m_batch, m_current), L"SettingsSectionHeaderStyle");
    column.Children().Append(view.headerText);

    StackPanel stage;
    stage.Padding({16, 16, 16, 16});
    stage.Spacing(12);
    view.progressBar = ProgressBar();
    view.progressBar.Minimum(0);
    view.progressBar.Maximum(100);
    stage.Children().Append(view.progressBar);

    Grid status = lineGrid();
    view.phaseText = styledTextBlock(transcribePhaseLabel(m_phase), L"SettingsCardBodyStyle");
    status.Children().Append(view.phaseText);
    view.percentText = secondaryTextBlock({}, L"SettingsCardDescriptionStyle", *view.host);
    Grid::SetColumn(view.percentText, 1);
    status.Children().Append(view.percentText);
    stage.Children().Append(status);

    // What the provider has heard so far, newest words in view, so a long
    // file visibly moves between percentage steps.
    view.partialText = styledTextBlock({}, L"SettingsCardBodyStyle");
    view.partialText.TextWrapping(TextWrapping::Wrap);
    view.partialText.IsTextSelectionEnabled(true);
    view.partialScroll = ScrollViewer();
    view.partialScroll.Height(kPartialHeight);
    view.partialScroll.Content(view.partialText);
    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(
        view.partialScroll, hs(transcribeText(TranscribeText::PartialName)));
    stage.Children().Append(view.partialScroll);

    StackPanel content;
    content.Children().Append(stage);
    view.queue = StackPanel();
    content.Children().Append(view.queue);
    column.Children().Append(cardContainer(content));
    refreshQueue(view);
    showProgress();
    showPartialText(view);

    Button cancel = textButton(transcribeText(TranscribeText::Cancel));
    cancel.HorizontalAlignment(HorizontalAlignment::Right);
    cancel.Margin({0, 24, 0, 0});
    cancel.Click([this](const auto &, const auto &) { m_controller->fileTranscription()->cancel(); });
    column.Children().Append(cancel);

    m_progressTimer.start();
}

void TranscribePane::setPartialText(const QString &text)
{
    m_partialText = text;
    for (const View &view : m_views) {
        showPartialText(view);
    }
}

// The transcript so far, or while nothing is heard yet, a dim line saying
// where it will appear. Newest words stay in view unless the reader has
// scrolled back.
void TranscribePane::showPartialText(const View &view)
{
    if (!view.partialText) {
        return;
    }
    const bool following = view.partialScroll.VerticalOffset() >= view.partialScroll.ScrollableHeight();
    if (m_partialText.isEmpty()) {
        view.partialText.Text(hs(transcribeText(TranscribeText::PartialPlaceholder)));
        if (const auto dim = themeBrush(L"SettingsCardDescriptionForeground", *view.host)) {
            view.partialText.Foreground(dim);
        }
    } else {
        view.partialText.Text(hs(m_partialText));
        view.partialText.ClearValue(TextBlock::ForegroundProperty());
    }
    if (following) {
        view.partialScroll.UpdateLayout();
        view.partialScroll.ChangeView(nullptr, view.partialScroll.ScrollableHeight(), nullptr, true);
    }
}

// The whole file's progress in every window: 100% only once it is
// transcribed, refined and saved.
void TranscribePane::showProgress()
{
    const qreal progress = m_fileFinished
        ? 1.0
        : m_progress.advance(overallFileProgress(m_fractionSent, m_phase, refinesTranscripts(m_batchOptions),
                                                 m_phaseClock.elapsed()));
    for (const View &view : m_views) {
        if (view.progressBar) {
            view.progressBar.Value(progress * 100);
            view.percentText.Text(hs(percentLabel(progress)));
        }
    }
}

void TranscribePane::setPhase(TranscribePhase phase)
{
    m_phase = phase;
    m_phaseClock.start();
    for (const View &view : m_views) {
        if (view.phaseText) {
            view.phaseText.Text(hs(transcribePhaseLabel(phase)));
        }
    }
    showProgress();
    refreshQueue();
}

// Runs a session event now, or after the finished file on screen has had its
// moment at 100%.
void TranscribePane::afterLanding(std::function<void()> event)
{
    if (m_landing) {
        m_afterLanding << std::move(event);
        return;
    }
    event();
}

void TranscribePane::land()
{
    m_landing = false;
    // A queued fileFinished lands the next file and stops the replay.
    while (!m_landing && !m_afterLanding.isEmpty()) {
        m_afterLanding.takeFirst()();
    }
}

void TranscribePane::refreshQueue()
{
    for (const View &view : m_views) {
        refreshQueue(view);
    }
}

// One row per file once there is more than one: done, failed, the phase of
// the current one, or waiting.
void TranscribePane::refreshQueue(const View &view)
{
    if (!view.queue) {
        return;
    }
    view.queue.Children().Clear();
    if (m_batch.size() < 2) {
        return;
    }
    for (int i = 0; i < m_batch.size(); ++i) {
        const TranscribeQueueState state = queueState(i, m_current, m_results);
        wchar_t mark = 0;
        switch (state) {
        case TranscribeQueueState::Waiting:
            break;
        case TranscribeQueueState::Current:
            mark = kGlyphCurrent;
            break;
        case TranscribeQueueState::Done:
            mark = kGlyphDone;
            break;
        case TranscribeQueueState::Failed:
            mark = kGlyphFailed;
            break;
        }
        // Every line follows the stage above it, so each gets the separator.
        Grid line = separatedGrid();
        line.Padding({16, 12, 16, 12});
        line.ColumnSpacing(12);
        addColumn(line, {24, GridUnitType::Pixel});
        addColumn(line, {1, GridUnitType::Star});
        addColumn(line, {0, GridUnitType::Auto});
        if (mark) {
            line.Children().Append(glyph(mark));
        }
        TextBlock name = state == TranscribeQueueState::Waiting
            ? secondaryTextBlock(QFileInfo(m_batch.at(i)).fileName(), L"SettingsCardBodyStyle", *view.host)
            : styledTextBlock(QFileInfo(m_batch.at(i)).fileName(), L"SettingsCardBodyStyle");
        Grid::SetColumn(name, 1);
        line.Children().Append(name);
        TextBlock stateText = secondaryTextBlock(queueStateLabel(state, transcribePhaseLabel(m_phase)),
                                                 L"SettingsCardDescriptionStyle", *view.host);
        Grid::SetColumn(stateText, 2);
        line.Children().Append(stateText);
        view.queue.Children().Append(line);
    }
}

Button TranscribePane::copyButton(const QString &label, const QString &text)
{
    Button button = textButton(label);
    button.Click([this, label, text](const IInspectable &sender, const auto &) {
        QGuiApplication::clipboard()->setText(text);
        Button clicked = sender.as<Button>();
        clicked.Content(box_value(hs(transcribeText(TranscribeText::Copied))));
        QTimer::singleShot(1500, this, [clicked, label] { clicked.Content(box_value(hs(label))); });
    });
    return button;
}

void TranscribePane::appendResults(const StackPanel &column, PaneHost &host)
{
    column.Children().Append(styledTextBlock(resultsTitle(int(m_results.size())), L"SettingsSectionHeaderStyle"));

    // Toolbar: Refined/Raw when refinement ran, Copy all and Export all.
    StackPanel top;
    top.Padding({16, 16, 16, 16});
    top.Spacing(8);
    Grid toolbar = lineGrid();
    if (refinesTranscripts(m_batchOptions)) {
        const QList<RowOption> versions{{QStringLiteral("refined"), transcribeText(TranscribeText::Refined)},
                                        {QStringLiteral("raw"), transcribeText(TranscribeText::Raw)}};
        toolbar.Children().Append(selectorBar(versions, m_showRaw ? QStringLiteral("raw") : QStringLiteral("refined"),
                                              [this](const QString &id) {
                                                  const bool showRaw = id == QStringLiteral("raw");
                                                  if (showRaw != m_showRaw) {
                                                      m_showRaw = showRaw;
                                                      rebuild();
                                                  }
                                              }));
    }
    StackPanel actions;
    actions.Orientation(Orientation::Horizontal);
    actions.Spacing(8);
    actions.Children().Append(copyButton(transcribeText(TranscribeText::CopyAll), allTranscripts(m_results, m_showRaw)));
    Button exportAllButton = textButton(transcribeText(TranscribeText::ExportAll));
    exportAllButton.Click([this, &host](const auto &, const auto &) { exportAll(host); });
    actions.Children().Append(exportAllButton);
    Grid::SetColumn(actions, 1);
    toolbar.Children().Append(actions);
    top.Children().Append(toolbar);
    top.Children().Append(secondaryTextBlock(
        batchSummary(m_results, int(m_batch.size()), m_cancelled, m_durationsMs, m_batchOptions, m_batchLabels),
        L"SettingsCardDescriptionStyle", host));
    column.Children().Append(cardContainer(top));

    if (!m_resultsProblem.isEmpty()) {
        InfoBar problem;
        problem.IsClosable(false);
        problem.IsOpen(true);
        problem.Severity(InfoBarSeverity::Error);
        problem.Message(hs(m_resultsProblem));
        problem.Margin({0, 8, 0, 0});
        column.Children().Append(problem);
    }

    // One Expander per file: the header names it and carries its actions, the
    // body is the transcript.
    StackPanel files;
    files.Spacing(4);
    files.Margin({0, 4, 0, 0});
    for (int i = 0; i < m_results.size(); ++i) {
        const TranscribeFileResult &result = m_results.at(i);
        const QString text = shownTranscript(result, m_showRaw);
        const QString meta = resultMeta(result, m_durationsMs.value(result.path, -1), m_showRaw);

        Grid header = lineGrid();
        StackPanel label;
        label.Spacing(2);
        label.VerticalAlignment(VerticalAlignment::Center);
        label.Children().Append(styledTextBlock(QFileInfo(result.path).fileName(), L"SettingsCardBodyStyle"));
        if (result.failed()) {
            TextBlock failure = styledTextBlock(meta, L"SettingsCardDescriptionStyle");
            if (const auto negative = themeBrush(L"NegativeTextForeground", host)) {
                failure.Foreground(negative);
            }
            label.Children().Append(failure);
        } else {
            label.Children().Append(secondaryTextBlock(meta, L"SettingsCardDescriptionStyle", host));
        }
        // A transcript that came through with a problem on the way (refinement
        // fell back to the raw text, saving failed) says so in its header,
        // visible without opening the row.
        if (!result.failed() && !result.error.isEmpty()) {
            InfoBar warning;
            warning.IsClosable(false);
            warning.IsOpen(true);
            warning.Severity(InfoBarSeverity::Warning);
            warning.Message(hs(result.error));
            label.Children().Append(warning);
        }
        header.Children().Append(label);
        StackPanel buttons;
        buttons.Orientation(Orientation::Horizontal);
        buttons.Spacing(8);
        buttons.VerticalAlignment(VerticalAlignment::Center);
        if (!result.savedPath.isEmpty()) {
            StackPanel saved;
            saved.Orientation(Orientation::Horizontal);
            saved.Spacing(4);
            saved.VerticalAlignment(VerticalAlignment::Center);
            FontIcon check = glyph(kGlyphDone);
            TextBlock savedText = styledTextBlock(transcribeText(TranscribeText::Saved), L"SettingsCardDescriptionStyle");
            if (const auto positive = themeBrush(L"PositiveTextForeground", host)) {
                check.Foreground(positive);
                savedText.Foreground(positive);
            }
            saved.Children().Append(check);
            saved.Children().Append(savedText);
            ToolTipService::SetToolTip(saved, box_value(hs(QDir::toNativeSeparators(result.savedPath))));
            buttons.Children().Append(saved);
        }
        if (result.failed()) {
            Button retryButton = textButton(transcribeText(i == m_retrying ? TranscribeText::Retrying
                                                                           : TranscribeText::Retry));
            retryButton.IsEnabled(m_retrying < 0);
            retryButton.Click([this, i](const auto &, const auto &) { retry(i); });
            buttons.Children().Append(retryButton);
        } else {
            buttons.Children().Append(copyButton(transcribeText(TranscribeText::Copy), text));
            Button exportButton = textButton(transcribeText(TranscribeText::Export));
            exportButton.Click([this, &host, path = result.path, text](const auto &, const auto &) {
                exportOne(host, path, text);
            });
            buttons.Children().Append(exportButton);
        }
        Grid::SetColumn(buttons, 1);
        header.Children().Append(buttons);

        Expander item;
        item.HorizontalAlignment(HorizontalAlignment::Stretch);
        item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        item.Header(header);
        TextBlock body = styledTextBlock(text, L"SettingsInfoTextStyle");
        body.IsTextSelectionEnabled(true);
        item.Content(body);
        // Disabling the Expander disables its header too, which carries Retry.
        item.IsEnabled(!body.Text().empty() || result.failed());
        item.IsExpanded(m_expanded.contains(i));
        item.Expanding([this, i](const auto &, const auto &) { m_expanded.insert(i); });
        item.Collapsed([this, i](const auto &, const auto &) { m_expanded.remove(i); });
        files.Children().Append(item);
    }
    column.Children().Append(files);

    Button again = textButton(transcribeText(TranscribeText::TranscribeMore));
    again.HorizontalAlignment(HorizontalAlignment::Right);
    again.Margin({0, 24, 0, 0});
    again.Click([this](const auto &, const auto &) {
        backToSetup();
        rebuild();
    });
    column.Children().Append(again);
}

winrt::fire_and_forget TranscribePane::chooseFiles(PaneHost &host)
{
    const std::weak_ptr<bool> weak = host.alive;
    try {
        Pickers::FileOpenPicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(host.hwnd()));
        picker.SuggestedStartLocation(Pickers::PickerLocationId::MusicLibrary);
        for (const QString &extension : transcribableExtensions()) {
            picker.FileTypeFilter().Append(hs(QStringLiteral(".") + extension));
        }
        const auto picked = co_await picker.PickMultipleFilesAsync();
        if (gone(weak)) {
            co_return;
        }
        QStringList paths;
        for (const auto &file : picked) {
            paths << qs(file.Path());
        }
        addFiles(paths);
    } catch (const winrt::hresult_error &error) {
        qWarning() << "choosing audio files failed:" << qs(error.message());
    }
}

winrt::fire_and_forget TranscribePane::chooseFolder(PaneHost &host)
{
    const std::weak_ptr<bool> weak = host.alive;
    try {
        Pickers::FolderPicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(host.hwnd()));
        picker.SuggestedStartLocation(Pickers::PickerLocationId::DocumentsLibrary);
        picker.FileTypeFilter().Append(L"*");
        const auto folder = co_await picker.PickSingleFolderAsync();
        if (gone(weak)) {
            co_return;
        }
        if (folder) {
            m_folder = qs(folder.Path());
        } else if (m_folder.isEmpty()) {
            // Cancelled with no folder to fall back on.
            m_destination = TranscriptDestination::BesideInput;
        }
        rebuild();
    } catch (const winrt::hresult_error &error) {
        qWarning() << "choosing a folder failed:" << qs(error.message());
    }
}

winrt::fire_and_forget TranscribePane::exportOne(PaneHost &host, QString audioPath, QString text)
{
    const std::weak_ptr<bool> weak = host.alive;
    try {
        Pickers::FileSavePicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(host.hwnd()));
        picker.SuggestedFileName(hs(QFileInfo(audioPath).completeBaseName() + QStringLiteral("-transcribed")));
        picker.FileTypeChoices().Insert(hs(transcribeText(TranscribeText::TextFiles)), winrt::single_threaded_vector<hstring>({hstring(L".txt")}));
        const auto file = co_await picker.PickSaveFileAsync();
        if (gone(weak) || !file) {
            co_return;
        }
        m_resultsProblem = writeText(qs(file.Path()), text);
        if (!m_resultsProblem.isEmpty()) {
            rebuild();
        }
    } catch (const winrt::hresult_error &error) {
        qWarning() << "exporting a transcript failed:" << qs(error.message());
    }
}

winrt::fire_and_forget TranscribePane::exportAll(PaneHost &host)
{
    const std::weak_ptr<bool> weak = host.alive;
    try {
        Pickers::FolderPicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(host.hwnd()));
        picker.FileTypeFilter().Append(L"*");
        const auto folder = co_await picker.PickSingleFolderAsync();
        if (gone(weak) || !folder) {
            co_return;
        }
        QStringList errors;
        for (const TranscribeFileResult &result : std::as_const(m_results)) {
            QString error;
            if (!result.failed()) {
                saveTranscript(result.path, qs(folder.Path()), shownTranscript(result, m_showRaw), &error);
            }
            if (!error.isEmpty()) {
                errors << error;
            }
        }
        m_resultsProblem = errors.join(QLatin1Char('\n'));
        rebuild();
    } catch (const winrt::hresult_error &error) {
        qWarning() << "exporting transcripts failed:" << qs(error.message());
    }
}

winrt::fire_and_forget TranscribePane::dropFiles(PaneHost &host, DragEventArgs args)
{
    const std::weak_ptr<bool> weak = host.alive;
    if (!args.DataView().Contains(StandardDataFormats::StorageItems())) {
        co_return;
    }
    // The drop's data is only readable until the deferral completes.
    auto deferral = args.GetDeferral();
    QStringList paths;
    try {
        for (const auto &item : co_await args.DataView().GetStorageItemsAsync()) {
            paths << qs(item.Path());
        }
    } catch (const winrt::hresult_error &error) {
        qWarning() << "reading dropped files failed:" << qs(error.message());
    }
    deferral.Complete();
    if (!gone(weak)) {
        addFiles(paths);
    }
}

} // namespace speecher::win
