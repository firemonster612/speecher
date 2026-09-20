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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QResizeEvent>
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

QVBoxLayout *makePage(QWidget *page, const QString &description)
{
    auto *layout = new QVBoxLayout(page);
    const int margin = setupPageMargin();
    layout->setContentsMargins(margin, margin, margin, margin);
    layout->setSpacing(16);

    auto *intro = new QLabel(description, page);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    return layout;
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

// One selectable provider: the name on the left, what the probe found on the
// right. Both pages' option lists are built from these.
ProviderOptionRow addOptionRow(QVBoxLayout *layout,
                               QButtonGroup *group,
                               QWidget *parent,
                               const QString &id,
                               const QString &label,
                               const QString &objectNamePrefix)
{
    auto *button = new QRadioButton(label, parent);
    button->setObjectName(objectNamePrefix + QStringLiteral("Option_") + id);
    QFont font = button->font();
    font.setBold(true);
    button->setFont(font);
    auto *status = new QLabel(parent);
    status->setObjectName(objectNamePrefix + QStringLiteral("Status_") + id);
    auto *row = new QHBoxLayout;
    row->addWidget(button);
    row->addStretch();
    row->addWidget(status);
    layout->addLayout(row);
    group->addButton(button);
    return {id, label, button, status};
}

void addOptionNote(QVBoxLayout *layout, QWidget *parent, const QString &text)
{
    if (text.isEmpty()) {
        return;
    }
    auto *note = new QLabel(text, parent);
    note->setWordWrap(true);
    note->setFont(settings::smallFont(note->font()));
    note->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(note);
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
    auto *prerequisites = new QGroupBox(QStringLiteral("Before you start"), this);
    auto *box = new QVBoxLayout(prerequisites);
    box->setSpacing(settings::smallSpacing());
    auto *lead = new WrappingLabel(
        QStringLiteral("Speecher uses your existing ChatGPT or Claude sign-in. Install and sign in to one of these, then choose Check again:"),
        prerequisites);
    lead->setWordWrap(true);
    box->addWidget(lead);

    for (const ProviderDescriptor &provider : m_providers.speechProviders()) {
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(credentialSourceLabel(provider.id, provider.label),
                                  prerequisites));
        row->addStretch();
        auto *status = new QLabel(QStringLiteral("Checking…"), prerequisites);
        status->setObjectName(QStringLiteral("welcomeCredentialStatus_") + provider.id);
        row->addWidget(status);
        box->addLayout(row);

        auto *hint = new WrappingLabel(provider.setupHint, prerequisites);
        hint->setObjectName(QStringLiteral("welcomeCredentialHint_") + provider.id);
        hint->setWordWrap(true);
        hint->setFont(settings::smallFont(hint->font()));
        hint->setForegroundRole(QPalette::PlaceholderText);
        hint->hide();
        box->addWidget(hint);
        m_rows.append({provider.id, status, hint, false});
    }

    auto *checkAgain = new QPushButton(QStringLiteral("Check again"), prerequisites);
    checkAgain->setObjectName(QStringLiteral("welcomeCheckAgain"));
    checkAgain->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    connect(checkAgain, &QPushButton::clicked, this, &WelcomeSetupPage::checkCredentials);
    box->addWidget(checkAgain, 0, Qt::AlignLeft);

    layout->addWidget(prerequisites);
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
    auto *choices = new QGroupBox(QStringLiteral("Transcription service"), this);
    auto *choiceLayout = new QVBoxLayout(choices);
    choiceLayout->setSpacing(settings::smallSpacing());
    auto *group = new QButtonGroup(this);
    const QString savedProvider = m_settings.speechProvider();
    for (const ProviderDescriptor &provider : m_providers.speechProviders()) {
        m_options.append(addOptionRow(choiceLayout, group, choices, provider.id,
                                      provider.label, QStringLiteral("speechProvider")));
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
    layout->addWidget(choices);
    layout->addWidget(m_stats);
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
        QStringLiteral("Speecher pastes your dictation into the app you are using. On Linux that works through the desktop accessibility service (AT-SPI), which also lets Speecher see where your cursor is and learn your corrections."));
#endif
    m_status->setWordWrap(true);
    m_enable->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    layout->addWidget(m_status);
#ifdef Q_OS_WIN
    m_enable->hide();
#else
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
        status = QStringLiteral("Accessibility is off, so Speecher can copy your dictation but not paste it. Turn it on to continue.");
        m_enable->setEnabled(true);
        m_enable->setText(QStringLiteral("Enable permanently"));
    }
#endif
    m_status->setText(m_lastError.isEmpty() ? status : m_lastError);
    if (wasComplete != stepComplete()) {
        emit stepCompleteChanged();
    }
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

    auto *formatRow = new QHBoxLayout;
    formatRow->addWidget(new QLabel(QStringLiteral("Clipboard format"), this));
    formatRow->addWidget(m_format, 1);
    layout->addWidget(m_status);
    layout->addWidget(m_progress);
    layout->addWidget(m_setup, 0, Qt::AlignLeft);
    layout->addWidget(m_clipboardOnly);
    layout->addSpacing(8);
    layout->addLayout(formatRow);
    layout->addWidget(m_restoreClipboard);
    layout->addStretch();
#ifndef SPEECHER_WITH_YDOTOOL
    // Nothing to install and nothing to opt out of.
    m_clipboardOnly->hide();
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
    m_clipboardOnly->setVisible(!status.ready());
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
    , m_none(new QRadioButton(QStringLiteral("None"), this))
    , m_stats(new ProviderStatsBlock(this))
    , m_warning(new WrappingLabel(this))
    , m_fastMode(new QCheckBox(QStringLiteral("Fast mode"), this))
    , m_fastModeHint(new QLabel(this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Refinement can clean up a raw transcript after dictation. Choose a provider, or None to skip cleanup."));

    auto *choices = new QGroupBox(QStringLiteral("Cleanup provider"), this);
    auto *choiceLayout = new QVBoxLayout(choices);
    choiceLayout->setSpacing(settings::smallSpacing());
    auto *group = new QButtonGroup(this);
    const QString savedProvider = m_settings.refinementProvider();
    for (const ProviderDescriptor &provider : providers.refinementProviders()) {
        m_options.append(addOptionRow(choiceLayout, group, choices, provider.id,
                                      provider.label, QStringLiteral("refinementProvider")));
        m_options.last().button->setChecked(provider.id == savedProvider);
        // The brands differ from the transcription page's, so say which
        // sign-in each one actually uses.
        addOptionNote(choiceLayout, choices, provider.setupHint);
    }

    m_none->setObjectName(QStringLiteral("refinementProviderOption_none"));
    QFont noneFont = m_none->font();
    noneFont.setBold(true);
    m_none->setFont(noneFont);
    auto *noneStatus = new QLabel(QStringLiteral("No cleanup"), choices);
    noneStatus->setObjectName(QStringLiteral("refinementProviderStatus_none"));
    auto *noneRow = new QHBoxLayout;
    noneRow->addWidget(m_none);
    noneRow->addStretch();
    noneRow->addWidget(noneStatus);
    choiceLayout->addLayout(noneRow);
    group->addButton(m_none);
    m_none->setChecked(selectedIndex() < 0);

    layout->addWidget(choices);
    layout->addWidget(m_stats);
    m_warning->setObjectName(QStringLiteral("refinementProviderWarning"));
    m_warning->setWordWrap(true);
    m_warning->hide();
    layout->addWidget(m_warning);
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
    , m_shortcutStatus(new QLabel(this))
    , m_signInNote(new QLabel(this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Setup is complete."));
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
    layout->addWidget(m_shortcutStatus);
#ifdef Q_OS_LINUX
    layout->addWidget(m_manualCommand);
    layout->addWidget(m_trayNote);
#endif
    layout->addWidget(m_signInNote);
    layout->addStretch();
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
