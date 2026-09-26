#include "frontend/win/TranscribePane.h"

#include "app/ApplicationController.h"
#include "core/SettingsStore.h"
#include "core/Target.h"
#include "core/settings/SettingsSchema.h"
#include "frontend/win/SettingsPage.h"
#include "providers/ProviderRegistry.h"

#include <QClipboard>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QRegularExpression>
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

const QString kSaveHint = QStringLiteral("Each transcript is saved as ⟨name⟩-transcribed.txt");
const wchar_t *const kAudioExtensions[] = {L".wav", L".mp3", L".m4a", L".mp4", L".aac", L".flac",
                                           L".ogg", L".oga", L".opus", L".webm"};
// Same dot geometry as the dictation panel's bars.
constexpr double kBarWidth = 3.2;
constexpr double kBarDotHeight = 3.2;

QString durationLabel(qint64 ms)
{
    const qint64 seconds = (ms + 500) / 1000;
    return seconds >= 60 ? QStringLiteral("%1 min %2 s").arg(seconds / 60).arg(seconds % 60)
                         : QStringLiteral("%1 s").arg(seconds);
}

int wordCount(const QString &text)
{
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    return int(text.split(whitespace, Qt::SkipEmptyParts).size());
}

QString writeText(const QString &path, const QString &text)
{
    QSaveFile file(path);
    const QByteArray bytes = text.toUtf8() + '\n';
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        return QStringLiteral("Could not save %1: %2").arg(QDir::toNativeSeparators(path), file.errorString());
    }
    return {};
}

QString providerLabel(const QList<ProviderDescriptor> &providers, const QString &id)
{
    for (const ProviderDescriptor &provider : providers) {
        if (provider.id == id) {
            return provider.label;
        }
    }
    return id;
}

bool gone(const std::weak_ptr<bool> &weak)
{
    const std::shared_ptr<bool> alive = weak.lock();
    return !alive || !*alive;
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

} // namespace

TranscribePane::TranscribePane(PaneHost &host, std::function<void()> rebuild)
    : m_host(host)
    , m_rebuild(std::move(rebuild))
{
    m_barTimer.setInterval(waveform::frameIntervalMs);
    m_barClock.start();
    connect(&m_barTimer, &QTimer::timeout, this, &TranscribePane::animateBars);

    FileTranscriptionSession *session = m_host.controller->fileTranscription();
    connect(session, &FileTranscriptionSession::fileStarted, this, [this](int index, const QString &) {
        m_current = index;
        m_peaks.clear();
        setProgress(0);
        if (m_headerText) {
            m_headerText.Text(hs(processingTitle()));
        }
        setPhase(QStringLiteral("Reading the audio…"));
    });
    connect(session, &FileTranscriptionSession::fileDecoded, this,
            [this](int index, const QVector<float> &peaks, qint64 durationMs) {
                m_durationsMs.insert(m_batch.value(index), durationMs);
                m_peaks = peaks;
                setPhase(QStringLiteral("Transcribing…"));
            });
    connect(session, &FileTranscriptionSession::fileProgress, this, [this](int, qreal fraction) {
        setProgress(fraction);
        if (fraction >= 1.0) {
            setPhase(QStringLiteral("Finishing the transcript…"));
        }
    });
    connect(session, &FileTranscriptionSession::fileRefining, this,
            [this] { setPhase(QStringLiteral("Refining…")); });
    connect(session, &FileTranscriptionSession::fileFinished, this,
            [this](int, const TranscribeFileResult &result) {
                m_results.append(result);
                refreshQueue();
            });
    connect(session, &FileTranscriptionSession::batchFinished, this,
            [this](const QList<TranscribeFileResult> &, bool cancelled) {
                m_current = -1;
                m_barTimer.stop();
                m_stage = cancelled ? Stage::Setup : Stage::Results;
                m_showRaw = false;
                m_expanded.clear();
                if (!m_results.isEmpty() && !m_results.first().refined.isEmpty()) {
                    m_expanded.insert(0);
                }
                m_resultsProblem.clear();
                m_rebuild();
            });
    seedOptions();
}

void TranscribePane::addFiles(const QStringList &paths)
{
    // Files opened over finished results start the next batch.
    if (m_stage == Stage::Results) {
        backToSetup();
    }
    for (const QString &path : paths) {
        const QString absolute = QFileInfo(path).absoluteFilePath();
        if (isAudioFile(absolute) && !m_files.contains(absolute)) {
            m_files << absolute;
        }
    }
    m_rebuild();
}

// The finished batch leaves the list; files added while it ran stay.
void TranscribePane::backToSetup()
{
    for (const QString &path : std::as_const(m_batch)) {
        m_files.removeOne(path);
    }
    seedOptions();
    m_stage = Stage::Setup;
}

void TranscribePane::enter()
{
    if (m_stage == Stage::Setup) {
        seedOptions();
    }
}

void TranscribePane::forgetElements()
{
    m_barTimer.stop();
    m_bars.clear();
    m_progressBar = nullptr;
    m_phaseText = nullptr;
    m_percentText = nullptr;
    m_headerText = nullptr;
    m_queue = nullptr;
}

void TranscribePane::seedOptions()
{
    const AppSettings settings = m_host.controller->settings()->snapshot();
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
        m_host.controller->settings()->snapshot().refinement.writingProfiles,
        writingProfileFromName(m_profile));
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
    m_results.clear();
    m_durationsMs.clear();
    m_current = -1;
    m_phase.clear();
    m_progress = 0;
    m_startError.clear();
    // Before start(): a batch whose files all fail at once finishes inside it.
    m_stage = Stage::Processing;
    if (!m_host.controller->startFileTranscription(m_batch, m_batchOptions, &m_startError)) {
        m_stage = Stage::Setup;
    }
    m_rebuild();
}

UIElement TranscribePane::build()
{
    forgetElements();
    StackPanel column;
    ScrollViewer scroll = pageScaffold(QStringLiteral("Transcribe"), column);
    switch (m_stage) {
    case Stage::Setup:
        appendSetup(column);
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
        scroll.Drop([this](const IInspectable &, const DragEventArgs &args) { dropFiles(args); });
        break;
    case Stage::Processing:
        appendProcessing(column);
        break;
    case Stage::Results:
        appendResults(column);
        break;
    }
    return scroll;
}

void TranscribePane::appendSetup(const StackPanel &column)
{
    PaneHost &host = m_host;
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
    Button browse = textButton(m_files.isEmpty() ? QStringLiteral("Browse…") : QStringLiteral("Add more…"));
    browse.Click([this](const auto &, const auto &) { chooseFiles(); });
    files.Children().Append(row(m_files.isEmpty() ? QStringLiteral("Choose audio files")
                                                  : QStringLiteral("Add more files"),
                                QStringLiteral("Drop files here or browse · wav, mp3, m4a, flac, ogg"),
                                browse, false));
    for (const QString &path : std::as_const(m_files)) {
        const QFileInfo info(path);
        Button remove;
        remove.Content(glyph(L''));
        ToolTipService::SetToolTip(remove, box_value(L"Remove"));
        Microsoft::UI::Xaml::Automation::AutomationProperties::SetName(remove, L"Remove");
        remove.Click([this, path](const auto &, const auto &) {
            m_files.removeOne(path);
            m_rebuild();
        });
        files.Children().Append(
            row(info.fileName(),
                QLocale().formattedDataSize(info.size(), 1, QLocale::DataSizeSIFormat),
                remove, true));
    }
    card(QStringLiteral("Audio files"), files);

    // Transcription.
    ProviderRegistry *registry = m_host.controller->providerRegistry();
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
                                                  m_rebuild();
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
    const RefinementSettings refinement = m_host.controller->settings()->snapshot().refinement;
    const QString model = m_refiner == QStringLiteral("openai")      ? refinement.openAiModel
                        : m_refiner == QStringLiteral("anthropic") ? refinement.anthropicModel
                                                                   : QString();
    const bool refining = m_refiner != QStringLiteral("none");
    StackPanel refine;
    refine.Children().Append(row(QStringLiteral("Provider"),
                                 QStringLiteral("Clean up the raw transcripts with a language model"),
                                 comboBox(refinerOptions, m_refiner,
                                          [this](const QString &id) {
                                              if (id != m_refiner) {
                                                  m_refiner = id;
                                                  m_rebuild();
                                              }
                                          }),
                                 false));
    if (!model.isEmpty()) {
        refine.Children().Append(row(QStringLiteral("Model"),
                                     QStringLiteral("Change it on the Refinement page"),
                                     secondaryTextBlock(model, L"SettingsInfoTextStyle", m_host),
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
                                                  m_rebuild();
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
        {QString::number(int(TranscriptDestination::Folder)), QStringLiteral("One folder…")},
        {QString::number(int(TranscriptDestination::None)), QStringLiteral("Just show them here")},
    };
    StackPanel output;
    output.Children().Append(row(QStringLiteral("Save transcripts"),
                                 m_destination == TranscriptDestination::None
                                     ? QStringLiteral("Copy or export from the results afterwards")
                                     : kSaveHint,
                                 comboBox(destinations, QString::number(int(m_destination)),
                                          [this](const QString &id) {
                                              const auto destination = TranscriptDestination(id.toInt());
                                              if (destination == m_destination) {
                                                  return;
                                              }
                                              m_destination = destination;
                                              if (destination == TranscriptDestination::Folder
                                                  && m_folder.isEmpty()) {
                                                  chooseFolder();
                                              }
                                              m_rebuild();
                                          }),
                                 false));
    if (m_destination == TranscriptDestination::Folder) {
        Button change = textButton(QStringLiteral("Change…"));
        change.Click([this](const auto &, const auto &) { chooseFolder(); });
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

void TranscribePane::appendProcessing(const StackPanel &column)
{
    m_headerText = styledTextBlock(processingTitle(), L"SettingsSectionHeaderStyle");
    column.Children().Append(m_headerText);

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
        m_bars.push_back(bar);
    }
    stage.Children().Append(bars);

    m_progressBar = ProgressBar();
    m_progressBar.Minimum(0);
    m_progressBar.Maximum(100);
    m_progressBar.Value(m_progress * 100);
    stage.Children().Append(m_progressBar);

    Grid status = lineGrid();
    m_phaseText = styledTextBlock(m_phase, L"SettingsCardBodyStyle");
    status.Children().Append(m_phaseText);
    m_percentText = secondaryTextBlock(QStringLiteral("%1%").arg(qRound(m_progress * 100)),
                                       L"SettingsCardDescriptionStyle", m_host);
    Grid::SetColumn(m_percentText, 1);
    status.Children().Append(m_percentText);
    stage.Children().Append(status);

    StackPanel content;
    content.Children().Append(stage);
    m_queue = StackPanel();
    content.Children().Append(m_queue);
    column.Children().Append(cardContainer(content));
    refreshQueue();

    Button cancel = textButton(QStringLiteral("Cancel"));
    cancel.HorizontalAlignment(HorizontalAlignment::Center);
    cancel.Margin({0, 24, 0, 0});
    cancel.Click([this](const auto &, const auto &) { m_host.controller->fileTranscription()->cancel(); });
    column.Children().Append(cancel);

    m_lastFrame = m_barClock.elapsed();
    m_level.restart(m_lastFrame);
    m_barTimer.start();
}

QString TranscribePane::processingTitle() const
{
    if (m_current < 0) {
        return QStringLiteral("Transcribing");
    }
    const QString name = QFileInfo(m_batch.value(m_current)).fileName();
    return m_batch.size() > 1
        ? QStringLiteral("Transcribing · %1 (%2 of %3)").arg(name).arg(m_current + 1).arg(m_batch.size())
        : QStringLiteral("Transcribing · %1").arg(name);
}

void TranscribePane::setProgress(qreal fraction)
{
    m_progress = fraction;
    if (m_progressBar) {
        m_progressBar.Value(fraction * 100);
        m_percentText.Text(hs(QStringLiteral("%1%").arg(qRound(fraction * 100))));
    }
}

void TranscribePane::setPhase(const QString &phase)
{
    m_phase = phase;
    if (m_phaseText) {
        m_phaseText.Text(hs(phase));
    }
    refreshQueue();
}

// One row per file once there is more than one: done, failed, the phase of
// the current one, or waiting.
void TranscribePane::refreshQueue()
{
    if (!m_queue) {
        return;
    }
    m_queue.Children().Clear();
    if (m_batch.size() < 2) {
        return;
    }
    for (int i = 0; i < m_batch.size(); ++i) {
        wchar_t mark = 0;
        QString state = QStringLiteral("Waiting");
        if (i < m_results.size()) {
            const bool failed = m_results.at(i).failed();
            mark = failed ? L'' : L'';
            state = failed ? QStringLiteral("Failed") : QStringLiteral("Done");
        } else if (i == m_current) {
            mark = L'';
            state = m_phase;
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
        const bool waiting = i > m_current && i >= m_results.size();
        TextBlock name = waiting
            ? secondaryTextBlock(QFileInfo(m_batch.at(i)).fileName(), L"SettingsCardBodyStyle", m_host)
            : styledTextBlock(QFileInfo(m_batch.at(i)).fileName(), L"SettingsCardBodyStyle");
        Grid::SetColumn(name, 1);
        line.Children().Append(name);
        TextBlock stateText = secondaryTextBlock(state, L"SettingsCardDescriptionStyle", m_host);
        Grid::SetColumn(stateText, 2);
        line.Children().Append(stateText);
        m_queue.Children().Append(line);
    }
}

void TranscribePane::animateBars()
{
    if (m_bars.empty()) {
        m_barTimer.stop();
        return;
    }
    const qint64 now = m_barClock.elapsed();
    const float elapsed = std::clamp((now - m_lastFrame) / 1000.0f, 0.0f, 0.1f);
    m_lastFrame = now;
    if (!m_peaks.isEmpty()) {
        const int at = std::clamp(int(m_progress * m_peaks.size()), 0, int(m_peaks.size()) - 1);
        m_level.addChunk(m_progress < 1.0 ? m_peaks.at(at) : 0.0f);
    }
    m_barPhase = std::fmod(m_barPhase + elapsed, 1.0f);
    m_level.advance(now);
    for (int i = 0; i < int(m_bars.size()); ++i) {
        const float phase = m_barPhase - float(i) / waveform::barCount;
        const double height = kBarDotHeight * m_level.audioScale() * waveform::bulge(i)
            * waveform::waveMultiplier(phase - std::floor(phase));
        m_bars[i].Height(height);
        m_bars[i].RadiusY(height / 4);
    }
}

QString TranscribePane::shownText(const TranscribeFileResult &result) const
{
    return m_showRaw ? result.raw : result.refined;
}

QString TranscribePane::summary() const
{
    const int count = int(m_results.size());
    const int failed = int(std::count_if(m_results.cbegin(), m_results.cend(),
                                         [](const TranscribeFileResult &result) { return result.failed(); }));
    qint64 totalMs = 0;
    int saved = 0;
    for (const TranscribeFileResult &result : m_results) {
        totalMs += m_durationsMs.value(result.path, 0);
        saved += !result.savedPath.isEmpty();
    }
    ProviderRegistry *registry = m_host.controller->providerRegistry();
    QStringList parts;
    if (count > 1) {
        parts << QStringLiteral("%1 transcripts").arg(count - failed);
    }
    if (failed > 0) {
        parts << QStringLiteral("%1 failed").arg(failed);
    }
    if (totalMs > 0) {
        parts << QStringLiteral("%1 of audio").arg(durationLabel(totalMs));
    }
    parts << QStringLiteral("transcribed with %1")
                 .arg(providerLabel(registry->speechProviders(), m_batchOptions.speechProviderId));
    if (m_batchOptions.refinementProviderId != QStringLiteral("none")
        && m_batchOptions.cleanupStrength != QStringLiteral("none")) {
        parts << QStringLiteral("refined with %1")
                     .arg(providerLabel(registry->refinementProviders(), m_batchOptions.refinementProviderId));
    }
    if (saved > 0) {
        parts << (m_batchOptions.destination == TranscriptDestination::Folder
                      ? QStringLiteral("saved to %1").arg(QDir::toNativeSeparators(m_batchOptions.folder))
                      : QStringLiteral("saved next to each audio file"));
    }
    return parts.join(QStringLiteral(" · "));
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

void TranscribePane::appendResults(const StackPanel &column)
{
    column.Children().Append(styledTextBlock(
        m_results.size() > 1 ? QStringLiteral("Transcripts") : QStringLiteral("Transcript"),
        L"SettingsSectionHeaderStyle"));

    // Toolbar: Refined/Raw when refinement ran, Copy all and Export all.
    StackPanel top;
    top.Padding({16, 16, 16, 16});
    top.Spacing(8);
    Grid toolbar = lineGrid();
    if (m_batchOptions.refinementProviderId != QStringLiteral("none")
        && m_batchOptions.cleanupStrength != QStringLiteral("none")) {
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
                m_rebuild();
            }
        });
        toolbar.Children().Append(variants);
    }
    QStringList parts;
    for (const TranscribeFileResult &result : std::as_const(m_results)) {
        if (!result.failed()) {
            parts << (m_results.size() > 1
                          ? QStringLiteral("# %1\n\n%2").arg(QFileInfo(result.path).fileName(), shownText(result))
                          : shownText(result));
        }
    }
    StackPanel actions;
    actions.Orientation(Orientation::Horizontal);
    actions.Spacing(8);
    actions.Children().Append(copyButton(QStringLiteral("Copy all"), parts.join(QStringLiteral("\n\n\n"))));
    Button exportAllButton = textButton(QStringLiteral("Export all"));
    exportAllButton.Click([this](const auto &, const auto &) { exportAll(); });
    actions.Children().Append(exportAllButton);
    Grid::SetColumn(actions, 1);
    toolbar.Children().Append(actions);
    top.Children().Append(toolbar);
    top.Children().Append(secondaryTextBlock(summary(), L"SettingsCardDescriptionStyle", m_host));
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
        const QString text = shownText(result);
        const qint64 duration = m_durationsMs.value(result.path, -1);
        QString meta = result.failed() ? result.error : QStringLiteral("%1 words").arg(wordCount(text));
        if (duration >= 0 && !result.failed()) {
            meta.prepend(durationLabel(duration) + QStringLiteral(" · "));
        }

        Grid header = lineGrid();
        StackPanel label;
        label.Spacing(2);
        label.VerticalAlignment(VerticalAlignment::Center);
        label.Children().Append(styledTextBlock(QFileInfo(result.path).fileName(), L"SettingsCardBodyStyle"));
        label.Children().Append(secondaryTextBlock(meta, L"SettingsCardDescriptionStyle", m_host));
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
            saved.Children().Append(glyph(L''));
            saved.Children().Append(secondaryTextBlock(QStringLiteral("Saved"), L"SettingsCardDescriptionStyle", m_host));
            ToolTipService::SetToolTip(saved, box_value(hs(QDir::toNativeSeparators(result.savedPath))));
            buttons.Children().Append(saved);
        }
        if (!result.failed()) {
            buttons.Children().Append(copyButton(QStringLiteral("Copy"), text));
            Button exportButton = textButton(QStringLiteral("Export"));
            exportButton.Click([this, path = result.path, text](const auto &, const auto &) {
                exportOne(path, text);
            });
            buttons.Children().Append(exportButton);
        }
        Grid::SetColumn(buttons, 1);
        header.Children().Append(buttons);

        Expander item;
        item.HorizontalAlignment(HorizontalAlignment::Stretch);
        item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        item.Header(header);
        TextBlock body = styledTextBlock(text.isEmpty() ? result.raw : text, L"SettingsInfoTextStyle");
        body.IsTextSelectionEnabled(true);
        item.Content(body);
        item.IsEnabled(!body.Text().empty());
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
        m_rebuild();
    });
    column.Children().Append(again);
}

winrt::fire_and_forget TranscribePane::chooseFiles()
{
    const std::weak_ptr<bool> weak = m_host.alive;
    try {
        Pickers::FileOpenPicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(m_host.hwnd()));
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

winrt::fire_and_forget TranscribePane::chooseFolder()
{
    const std::weak_ptr<bool> weak = m_host.alive;
    try {
        Pickers::FolderPicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(m_host.hwnd()));
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
        m_rebuild();
    } catch (const winrt::hresult_error &error) {
        qWarning() << "choosing a folder failed:" << qs(error.message());
    }
}

winrt::fire_and_forget TranscribePane::exportOne(QString audioPath, QString text)
{
    const std::weak_ptr<bool> weak = m_host.alive;
    try {
        Pickers::FileSavePicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(m_host.hwnd()));
        picker.SuggestedFileName(hs(QFileInfo(audioPath).completeBaseName() + QStringLiteral("-transcribed")));
        picker.FileTypeChoices().Insert(L"Text file", winrt::single_threaded_vector<hstring>({hstring(L".txt")}));
        const auto file = co_await picker.PickSaveFileAsync();
        if (gone(weak) || !file) {
            co_return;
        }
        m_resultsProblem = writeText(qs(file.Path()), text);
        if (!m_resultsProblem.isEmpty()) {
            m_rebuild();
        }
    } catch (const winrt::hresult_error &error) {
        qWarning() << "exporting a transcript failed:" << qs(error.message());
    }
}

winrt::fire_and_forget TranscribePane::exportAll()
{
    const std::weak_ptr<bool> weak = m_host.alive;
    try {
        Pickers::FolderPicker picker;
        check_hresult(picker.as<::IInitializeWithWindow>()->Initialize(m_host.hwnd()));
        picker.FileTypeFilter().Append(L"*");
        const auto folder = co_await picker.PickSingleFolderAsync();
        if (gone(weak) || !folder) {
            co_return;
        }
        QStringList errors;
        for (const TranscribeFileResult &result : std::as_const(m_results)) {
            QString error;
            if (!result.failed()) {
                saveTranscript(result.path, qs(folder.Path()), shownText(result), &error);
            }
            if (!error.isEmpty()) {
                errors << error;
            }
        }
        m_resultsProblem = errors.join(QLatin1Char('\n'));
        m_rebuild();
    } catch (const winrt::hresult_error &error) {
        qWarning() << "exporting transcripts failed:" << qs(error.message());
    }
}

winrt::fire_and_forget TranscribePane::dropFiles(DragEventArgs args)
{
    const std::weak_ptr<bool> weak = m_host.alive;
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
