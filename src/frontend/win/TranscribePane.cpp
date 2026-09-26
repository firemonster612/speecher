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

#include <algorithm>
#include <cmath>

namespace speecher::win {

namespace {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using winrt::Windows::ApplicationModel::DataTransfer::DataPackageOperation;
using winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats;
namespace Pickers = winrt::Windows::Storage::Pickers;

// Non-ASCII text is written as \u escapes: MSVC reads a source without a BOM
// in the system code page.
const QString kSaveHint = QStringLiteral("Each transcript is saved as \u27e8name\u27e9-transcribed.txt");
// Segoe Fluent Icons.
constexpr wchar_t kGlyphRemove = L'\uE711';
constexpr wchar_t kGlyphDone = L'\uE73E';
constexpr wchar_t kGlyphFailed = L'\uE783';
constexpr wchar_t kGlyphCurrent = L'\uE768';
const wchar_t *const kAudioExtensions[] = {L".wav", L".mp3", L".m4a", L".mp4", L".aac", L".flac",
                                           L".ogg", L".oga", L".opus", L".webm"};
// Same dot geometry as the dictation panel's bars.
constexpr double kBarWidth = 3.2;
constexpr double kBarDotHeight = 3.2;
// How long a finished file shows at 100% before the next one replaces it.
constexpr int kLandingMs = 600;
QString writeText(const QString &path, const QString &text)
{
    QSaveFile file(path);
    const QByteArray bytes = text.toUtf8() + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return QStringLiteral("Could not save %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
    }
    return {};
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

// "1 Configure · 2 Transcribe · 3 Export" above the stage: the current step
// in bold, the steps behind it checked, the ones ahead dim, and a hint under
// Configure that it is only a check before pressing Transcribe.
void appendSteps(const StackPanel &column, TranscribeStep current, const PaneHost &host)
{
    StackPanel steps;
    steps.Orientation(Orientation::Horizontal);
    steps.Spacing(8);
    steps.HorizontalAlignment(HorizontalAlignment::Center);
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
            done.Children().Append(check);
            done.Children().Append(styledTextBlock(label, L"SettingsCardBodyStyle"));
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
        hint.HorizontalAlignment(HorizontalAlignment::Center);
        hint.Margin({0, 4, 0, 0});
        column.Children().Append(hint);
    }
}

QString percentLabel(qreal progress)
{
    return QStringLiteral("%1%").arg(int(progress * 100));
}

} // namespace

TranscribePane::TranscribePane(ApplicationController *controller)
    : m_controller(controller)
{
    m_barTimer.setInterval(waveform::frameIntervalMs);
    m_barClock.start();
    connect(&m_barTimer, &QTimer::timeout, this, &TranscribePane::animateBars);

    // Every event goes through afterLanding: a finished file holds at 100% for
    // a moment, and the session has already started the next one.
    FileTranscriptionSession *session = m_controller->fileTranscription();
    connect(session, &FileTranscriptionSession::fileStarted, this, [this](int index, const QString &path) {
        afterLanding([this, index, path] {
            m_current = index;
            m_currentPath = path;
            m_peaks.clear();
            m_fractionSent = 0;
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
            [this](int, const QVector<float> &peaks, qint64 durationMs) {
                afterLanding([this, peaks, durationMs] {
                    m_durationsMs.insert(m_currentPath, durationMs);
                    m_peaks = peaks;
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
                    QTimer::singleShot(kLandingMs, this, &TranscribePane::land);
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
                    m_barTimer.stop();
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
    const WritingProfileSettings profile = writingProfileSettingsFor(
        m_controller->settings()->snapshot().refinement.writingProfiles, writingProfileFromName(m_profile));
    m_cleanup = profile.cleanupStrength;
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
            }
        });
        scroll.Drop([this, &host](const IInspectable &, const DragEventArgs &args) { dropFiles(host, args); });
        break;
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
        // rowGrid shows disabledHelp in place of help on a disabled row.
        snapshot.disabledHelp = help;
        snapshot.enabled = enabled;
        return rowGrid(snapshot, control, host, follows);
    };
    const auto card = [&column](const QString &title, const StackPanel &rows) {
        column.Children().Append(styledTextBlock(title, L"SettingsSectionHeaderStyle"));
        column.Children().Append(cardContainer(rows));
    };

    // Audio files: a browse row, then one row per file with its size.
    StackPanel files;
    Button browse = textButton(m_files.isEmpty() ? QStringLiteral("Browse\u2026") : QStringLiteral("Add more\u2026"));
    browse.Click([this, &host](const auto &, const auto &) { chooseFiles(host); });
    files.Children().Append(row(m_files.isEmpty() ? QStringLiteral("Choose audio files")
                                                  : QStringLiteral("Add more files"),
                                QStringLiteral("Drop files here or browse \u00b7 wav, mp3, m4a, flac, ogg"),
                                browse, false));
    for (const QString &path : std::as_const(m_files)) {
        const QFileInfo info(path);
        Button remove;
        remove.Content(glyph(kGlyphRemove));
        ToolTipService::SetToolTip(remove, box_value(L"Remove"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(remove, L"Remove");
        remove.Click([this, path](const auto &, const auto &) {
            m_files.removeOne(path);
            rebuild();
        });
        files.Children().Append(
            row(info.fileName(), audioFileDetail(info.size(), m_durationsMs.value(path, -1)), remove, true));
    }
    card(QStringLiteral("Audio files"), files);

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
    speech.Children().Append(row(QStringLiteral("Model"), speechSummary,
                                 comboBox(speechOptions, m_speech,
                                          [this](const QString &id) {
                                              if (id != m_speech) {
                                                  m_speech = id;
                                                  rebuild();
                                              }
                                          }),
                                 false));
    ToggleSwitch vocabulary;
    vocabulary.OnContent(box_value(L""));
    vocabulary.OffContent(box_value(L""));
    vocabulary.MinWidth(0);
    vocabulary.IsOn(m_vocabulary);
    vocabulary.Toggled([this](const IInspectable &sender, const auto &) {
        m_vocabulary = sender.as<ToggleSwitch>().IsOn();
    });
    speech.Children().Append(row(QStringLiteral("Apply vocabulary"),
                                 QStringLiteral("Use your custom vocabulary and corrections on the result"),
                                 vocabulary, true));
    card(QStringLiteral("Transcription"), speech);

    // Refinement: the model is read-only here; cleanup, profile and tone only
    // matter with a provider chosen.
    QList<RowOption> refinerOptions{{QStringLiteral("none"), QStringLiteral("None")}};
    for (const ProviderDescriptor &provider : registry->refinementProviders()) {
        refinerOptions.append({provider.id, provider.label});
    }
    const QString model = refinementModel(m_refiner, m_controller->settings()->snapshot().refinement);
    const bool refining = m_refiner != QStringLiteral("none");
    StackPanel refine;
    refine.Children().Append(row(QStringLiteral("Provider"),
                                 QStringLiteral("Clean up the raw transcripts with a language model"),
                                 comboBox(refinerOptions, m_refiner,
                                          [this](const QString &id) {
                                              if (id != m_refiner) {
                                                  m_refiner = id;
                                                  rebuild();
                                              }
                                          }),
                                 false));
    if (!model.isEmpty()) {
        refine.Children().Append(row(QStringLiteral("Model"),
                                     QStringLiteral("Change it on the Refinement page"),
                                     secondaryTextBlock(model, L"SettingsInfoTextStyle", host),
                                     true));
    }
    refine.Children().Append(row(QStringLiteral("Cleanup"),
                                 QStringLiteral("How much the model may rewrite"),
                                 comboBox(cleanupStrengths(), m_cleanup,
                                          [this](const QString &id) { m_cleanup = id; }),
                                 true, refining));
    QList<RowOption> profiles;
    for (WritingProfile profile : {WritingProfile::Work, WritingProfile::Email, WritingProfile::Personal,
                                   WritingProfile::AiCoding, WritingProfile::Other}) {
        profiles.append({writingProfileName(profile), writingProfileLabel(profile)});
    }
    refine.Children().Append(row(QStringLiteral("Writing profile"),
                                 QStringLiteral("Sets cleanup and tone; you can still adjust them here"),
                                 comboBox(profiles, m_profile,
                                          [this](const QString &id) {
                                              if (id != m_profile) {
                                                  m_profile = id;
                                                  applyWritingProfile();
                                                  rebuild();
                                              }
                                          }),
                                 true, refining));
    refine.Children().Append(row(QStringLiteral("Tone"),
                                 QStringLiteral("Optional override on top of the profile"),
                                 comboBox(writingTones(), m_tone,
                                          [this](const QString &id) { m_tone = id; }),
                                 true, refining));
    card(QStringLiteral("Refinement"), refine);

    // Output.
    const QList<RowOption> destinations{
        {QString::number(int(TranscriptDestination::BesideInput)), QStringLiteral("Next to each audio file")},
        {QString::number(int(TranscriptDestination::Folder)), QStringLiteral("One folder\u2026")},
        {QString::number(int(TranscriptDestination::None)), QStringLiteral("Just show them here")},
    };
    StackPanel output;
    output.Children().Append(row(QStringLiteral("Save transcripts"),
                                 m_destination == TranscriptDestination::None
                                     ? QStringLiteral("Copy or export from the results afterwards")
                                     : kSaveHint,
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
        Button change = textButton(QStringLiteral("Change\u2026"));
        change.Click([this, &host](const auto &, const auto &) { chooseFolder(host); });
        output.Children().Append(row(QStringLiteral("Folder"), QDir::toNativeSeparators(m_folder), change, true));
    }
    card(QStringLiteral("Output"), output);

    if (!m_startError.isEmpty()) {
        InfoBar problem;
        problem.IsClosable(false);
        problem.IsOpen(true);
        problem.Severity(InfoBarSeverity::Error);
        problem.Message(hs(m_startError));
        problem.Margin({0, 16, 0, 0});
        column.Children().Append(problem);
    }
    Button start = textButton(m_files.size() > 1 ? QStringLiteral("Transcribe %1 files").arg(m_files.size())
                                                 : QStringLiteral("Transcribe"));
    start.Style(Application::Current().Resources().Lookup(box_value(L"AccentButtonStyle")).as<Style>());
    start.MinWidth(160);
    start.HorizontalAlignment(HorizontalAlignment::Center);
    start.Margin({0, 24, 0, 0});
    start.IsEnabled(!m_files.isEmpty());
    start.Click([this](const auto &, const auto &) { startBatch(); });
    column.Children().Append(start);
}

void TranscribePane::appendProcessing(const StackPanel &column, View &view)
{
    view.headerText = styledTextBlock(processingTitle(m_batch, m_current), L"SettingsSectionHeaderStyle");
    column.Children().Append(view.headerText);

    StackPanel stage;
    stage.Padding({16, 16, 16, 16});
    stage.Spacing(12);
    // The dictation panel's bars, driven by the level of the audio at the
    // point the upload has reached.
    StackPanel bars;
    bars.Orientation(Orientation::Horizontal);
    bars.Spacing(kBarWidth);
    bars.Height(48);
    bars.HorizontalAlignment(HorizontalAlignment::Center);
    Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(bars, L"Audio level");
    const auto fill = Application::Current()
                          .Resources()
                          .TryLookup(box_value(L"AccentFillColorDefaultBrush"))
                          .try_as<winrt::Microsoft::UI::Xaml::Media::Brush>();
    for (int i = 0; i < waveform::barCount; ++i) {
        Microsoft::UI::Xaml::Shapes::Rectangle bar;
        bar.Width(kBarWidth);
        bar.RadiusX(0.8);
        bar.RadiusY(0.8);
        bar.Height(kBarDotHeight);
        bar.VerticalAlignment(VerticalAlignment::Center);
        if (fill) {
            bar.Fill(fill);
        }
        bars.Children().Append(bar);
        view.bars.push_back(bar);
    }
    stage.Children().Append(bars);

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

    StackPanel content;
    content.Children().Append(stage);
    view.queue = StackPanel();
    content.Children().Append(view.queue);
    column.Children().Append(cardContainer(content));
    refreshQueue(view);
    showProgress();

    Button cancel = textButton(QStringLiteral("Cancel"));
    cancel.HorizontalAlignment(HorizontalAlignment::Center);
    cancel.Margin({0, 24, 0, 0});
    cancel.Click([this](const auto &, const auto &) { m_controller->fileTranscription()->cancel(); });
    column.Children().Append(cancel);

    m_lastFrame = m_barClock.elapsed();
    m_level.restart(m_lastFrame);
    m_barTimer.start();
}

// The whole file's progress in every window: 100% only once it is
// transcribed, refined and saved.
void TranscribePane::showProgress()
{
    const qreal progress = m_fileFinished
        ? 1.0
        : overallFileProgress(m_fractionSent, m_phase, refinesTranscripts(m_batchOptions), m_phaseClock.elapsed());
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

// Each frame: the bars follow the audio at the point the upload has reached,
// and the progress eases through the open-ended waits.
void TranscribePane::animateBars()
{
    const bool anyBars = std::any_of(m_views.begin(), m_views.end(),
                                     [](const View &view) { return !view.bars.empty(); });
    if (!anyBars) {
        m_barTimer.stop();
        return;
    }
    const qint64 now = m_barClock.elapsed();
    const float elapsed = std::clamp((now - m_lastFrame) / 1000.0f, 0.0f, 0.1f);
    m_lastFrame = now;
    if (!m_peaks.isEmpty()) {
        const bool sending = m_phase == TranscribePhase::Transcribing && !m_fileFinished;
        const int at = std::clamp(int(m_fractionSent * m_peaks.size()), 0, int(m_peaks.size()) - 1);
        m_level.addChunk(sending ? m_peaks.at(at) : 0.0f);
    }
    m_barPhase = std::fmod(m_barPhase + elapsed, 1.0f);
    m_level.advance(now);
    for (const View &view : m_views) {
        for (int i = 0; i < int(view.bars.size()); ++i) {
            const float phase = m_barPhase - float(i) / waveform::barCount;
            const double height = kBarDotHeight * m_level.audioScale() * waveform::bulge(i)
                * waveform::waveMultiplier(phase - std::floor(phase));
            view.bars[i].Height(height);
            view.bars[i].RadiusY(height / 4);
        }
    }
    showProgress();
}

Button TranscribePane::copyButton(const QString &label, const QString &text)
{
    Button button = textButton(label);
    button.Click([this, label, text](const IInspectable &sender, const auto &) {
        QGuiApplication::clipboard()->setText(text);
        Button clicked = sender.as<Button>();
        clicked.Content(box_value(L"Copied"));
        QTimer::singleShot(1500, this, [clicked, label] { clicked.Content(box_value(hs(label))); });
    });
    return button;
}

void TranscribePane::appendResults(const StackPanel &column, PaneHost &host)
{
    column.Children().Append(styledTextBlock(
        m_results.size() > 1 ? QStringLiteral("Transcripts") : QStringLiteral("Transcript"),
        L"SettingsSectionHeaderStyle"));

    // Toolbar: Refined/Raw when refinement ran, Copy all and Export all.
    StackPanel top;
    top.Padding({16, 16, 16, 16});
    top.Spacing(8);
    Grid toolbar = lineGrid();
    if (refinesTranscripts(m_batchOptions)) {
        SelectorBar variants;
        SelectorBarItem refined;
        refined.Text(L"Refined");
        SelectorBarItem raw;
        raw.Text(L"Raw");
        variants.Items().Append(refined);
        variants.Items().Append(raw);
        variants.SelectedItem(m_showRaw ? raw : refined);
        variants.SelectionChanged([this](const SelectorBar &sender, const auto &) {
            const auto selected = sender.SelectedItem();
            const bool showRaw = selected && selected.Text() == L"Raw";
            if (showRaw != m_showRaw) {
                m_showRaw = showRaw;
                rebuild();
            }
        });
        toolbar.Children().Append(variants);
    }
    StackPanel actions;
    actions.Orientation(Orientation::Horizontal);
    actions.Spacing(8);
    actions.Children().Append(copyButton(QStringLiteral("Copy all"), allTranscripts(m_results, m_showRaw)));
    Button exportAllButton = textButton(QStringLiteral("Export all"));
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
        label.Children().Append(secondaryTextBlock(meta, L"SettingsCardDescriptionStyle", host));
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
            saved.Children().Append(glyph(kGlyphDone));
            saved.Children().Append(secondaryTextBlock(QStringLiteral("Saved"), L"SettingsCardDescriptionStyle", host));
            ToolTipService::SetToolTip(saved, box_value(hs(QDir::toNativeSeparators(result.savedPath))));
            buttons.Children().Append(saved);
        }
        if (result.failed()) {
            Button retryButton = textButton(i == m_retrying ? QStringLiteral("Retrying\u2026")
                                                            : QStringLiteral("Retry"));
            retryButton.IsEnabled(m_retrying < 0);
            retryButton.Click([this, i](const auto &, const auto &) { retry(i); });
            buttons.Children().Append(retryButton);
        } else {
            buttons.Children().Append(copyButton(QStringLiteral("Copy"), text));
            Button exportButton = textButton(QStringLiteral("Export"));
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

    Button again = textButton(QStringLiteral("Transcribe more files"));
    again.HorizontalAlignment(HorizontalAlignment::Center);
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
        for (const wchar_t *extension : kAudioExtensions) {
            picker.FileTypeFilter().Append(extension);
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
        picker.FileTypeChoices().Insert(L"Text file", winrt::single_threaded_vector<hstring>({hstring(L".txt")}));
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
