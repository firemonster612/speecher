#include "ui/setup/SetupPages.h"

#include "app/ApplicationController.h"
#include "app/PlatformComposition.h"
#include "core/SettingsStore.h"
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

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QThread>
#include <QVBoxLayout>

#ifdef Q_OS_MACOS
#include <QApplication>
#include <QDesktopServices>
#include <QElapsedTimer>
#include <QPermissions>
#include <QTimer>
#include <QUrl>
#endif

#include <algorithm>
#include <memory>

namespace speecher {

int setupPageMargin()
{
    return 24;
}

namespace {

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

#ifdef Q_OS_MACOS
constexpr auto microphonePaneUrl =
    "x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone";
#endif

#ifdef Q_OS_MACOS
QString shortcutHint()
{
    // Qt maps Meta to Control and Alt to Option on macOS, so the shared default
    // sequence reaches the user as Ctrl+Option+D.
    return QStringLiteral("Tap the shortcut to start dictation and tap it again to stop, or hold it and talk — dictation ends when you let go. The default reads as Ctrl+Option+D on this keyboard.");
}

QString shortcutFailureHint()
{
    return QStringLiteral("Could not register the shortcut: %1. Another app probably owns that combination. Change the sequence and try again, or click Finish again to continue without it.");
}
#endif

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

WelcomeSetupPage::WelcomeSetupPage(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Speecher records a short dictation, turns it into text, and sends it to the app you were using."));
    QString checksText = QStringLiteral(
        "This assistant checks your transcription provider, microphone, desktop accessibility, text delivery, refinement, and writing profiles.");
#ifdef Q_OS_LINUX
    checksText += QStringLiteral(" It ends by setting up a Global Shortcut.");
#endif
    auto *checks = new QLabel(checksText, this);
    checks->setWordWrap(true);
    layout->addWidget(checks);
    layout->addStretch();
}

SpeechProviderSetupPage::SpeechProviderSetupPage(SettingsStore &settings,
                                                 ProviderRegistry &providers,
                                                 QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_providers(providers)
    , m_provider(new QComboBox(this))
    , m_hint(new QLabel(this))
    , m_status(new QLabel(this))
    , m_checkAgain(new QPushButton(QStringLiteral("Check again"), this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Choose the service Speecher uses to turn speech into a Raw Transcript."));
    m_provider->setObjectName(QStringLiteral("speechProvider"));
    m_provider->setMinimumContentsLength(24);
    for (const ProviderDescriptor &provider : m_providers.speechProviders()) {
        m_provider->addItem(provider.label, provider.id);
    }
    settings::selectData(m_provider, m_settings.speechProvider());

    m_hint->setObjectName(QStringLiteral("speechProviderHint"));
    m_hint->setWordWrap(true);
    m_status->setWordWrap(true);
    m_checkAgain->setObjectName(QStringLiteral("speechProviderCheckAgain"));
    m_checkAgain->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    auto *providerRow = new QHBoxLayout;
    providerRow->addWidget(new QLabel(QStringLiteral("Transcription service"), this));
    providerRow->addWidget(m_provider, 1);
    layout->addLayout(providerRow);
    layout->addWidget(m_status);
    layout->addWidget(m_hint);
    layout->addWidget(m_checkAgain, 0, Qt::AlignLeft);
    layout->addStretch();
    connect(m_provider, &QComboBox::currentIndexChanged,
            this, &SpeechProviderSetupPage::updateProvider);
    connect(m_checkAgain, &QPushButton::clicked,
            this, &SpeechProviderSetupPage::checkProvider);
    updateProvider();
}

void SpeechProviderSetupPage::updateProvider()
{
    const QString providerId = m_provider->currentData().toString();
    m_settings.setSpeechProvider(providerId);
    const QList<ProviderDescriptor> providers = m_providers.speechProviders();
    const auto it = std::find_if(providers.cbegin(), providers.cend(),
                                 [&providerId](const ProviderDescriptor &provider) {
                                     return provider.id == providerId;
                                 });
    m_hint->setText(it == providers.cend() ? QString() : it->setupHint);
    checkProvider();
}

void SpeechProviderSetupPage::checkProvider()
{
    m_hint->show();
    m_checkAgain->show();
    const quint64 generation = ++m_checkGeneration;
    const QString providerId = m_provider->currentData().toString();
    SpeechTranscriber *provider = m_providers.speechProvider(
        providerId);
    if (!provider) {
        setStatusColor(m_status, false);
        m_status->setText(QStringLiteral("No transcription service is available."));
        return;
    }

    const SpeechSettings settings = m_settings.snapshot().speech;
    std::optional<SpeechPrepareJob> job = provider->createPrepareJob(settings);
    if (!job || !job->run) {
        const SpeechPrepareResult result = provider->prepare(settings);
        setStatusColor(m_status, result.ok);
        m_status->setText(result.ok
                              ? QStringLiteral("%1 is ready.").arg(provider->label())
                              : result.message);
        m_hint->setVisible(!result.ok);
        m_checkAgain->setVisible(!result.ok);
        return;
    }

    setStatusColor(m_status, false);
    m_status->setText(QStringLiteral("Checking…"));
    const QString providerLabel = provider->label();
    auto result = std::make_shared<SpeechPrepareResult>();
    auto prepareJob = std::make_shared<SpeechPrepareJob>(std::move(*job));
    QThread *thread = QThread::create([prepareJob, result] {
        *result = prepareJob->run();
    });
    connect(thread, &QThread::finished, this, [this, generation, providerId, providerLabel, prepareJob, result] {
        if (generation != m_checkGeneration
            || providerId != m_provider->currentData().toString()) {
            return;
        }
        if (prepareJob->apply) {
            prepareJob->apply(*result);
        }
        setStatusColor(m_status, result->ok);
        m_status->setText(result->ok
                              ? QStringLiteral("%1 is ready.").arg(providerLabel)
                              : result->message);
        m_hint->setVisible(!result->ok);
        m_checkAgain->setVisible(!result->ok);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
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
#ifdef Q_OS_MACOS
    , m_inputVolumeStatus(new QLabel(this))
#endif
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Choose the input Speecher should record. Speak normally and check that the level moves."));
    m_device->setMinimumContentsLength(28);
    m_level->setRange(0, 100);
    m_level->setValue(0);
    m_level->setFormat(QStringLiteral("Input level %p%"));
    m_status->setWordWrap(true);

#ifdef Q_OS_MACOS
    addMicrophonePermissionControls(layout);
#endif

    auto *form = new QGridLayout;
    form->addWidget(new QLabel(QStringLiteral("Microphone"), this), 0, 0);
    form->addWidget(m_device, 0, 1);
    form->addWidget(new QLabel(QStringLiteral("Live level"), this), 1, 0);
    form->addWidget(m_level, 1, 1);
#ifdef Q_OS_MACOS
    m_inputVolumeStatus->setObjectName(QStringLiteral("inputVolumeStatus"));
    m_inputVolumeStatus->setWordWrap(true);
    m_inputVolumeStatus->hide();
    form->addWidget(m_inputVolumeStatus, 2, 1);
#endif
    layout->addLayout(form);
    layout->addWidget(m_status);
    layout->addStretch();

    m_input = m_platform.createAudioInput(&m_settings, this);
#ifdef Q_OS_MACOS
    auto inputVolumeRefresh = std::make_shared<QElapsedTimer>();
#endif
    connect(m_input, &AudioInput::levelChanged, this, [this
#ifdef Q_OS_MACOS
                                                       , inputVolumeRefresh
#endif
    ](float level) {
        m_level->setValue(qBound(0, qRound(level * 100.0f), 100));
#ifdef Q_OS_MACOS
        if (!inputVolumeRefresh->isValid() || inputVolumeRefresh->elapsed() >= 500) {
            inputVolumeRefresh->restart();
            refreshInputVolume();
        }
#endif
        if (level > 0.01f) {
            m_status->setText(QStringLiteral("Microphone input detected."));
        }
    });
    connect(m_input, &AudioInput::failed, this, [this](const QString &message) {
        m_status->setText(message);
        m_level->setValue(0);
    });
    connect(m_device, &QComboBox::currentIndexChanged, this, [this] {
        m_settings.setAudioInputDeviceId(m_device->currentData().toString());
        if (m_active) {
            startMeter();
        }
    });
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
    }
}

#ifdef Q_OS_MACOS
void MicrophoneSetupPage::addMicrophonePermissionControls(QVBoxLayout *layout)
{
    m_permissionStatus = new QLabel(this);
    m_permissionStatus->setWordWrap(true);
    m_allowMicrophone = new QPushButton(QStringLiteral("Allow microphone access"), this);
    m_openMicrophoneSettings = new QPushButton(QStringLiteral("Open Microphone settings"), this);
    auto *buttons = new QHBoxLayout;
    for (QPushButton *button : {m_allowMicrophone, m_openMicrophoneSettings}) {
        button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        buttons->addWidget(button);
    }
    buttons->addStretch();
    layout->addWidget(m_permissionStatus);
    layout->addLayout(buttons);

    connect(m_allowMicrophone, &QPushButton::clicked, this, [this] {
        qApp->requestPermission(QMicrophonePermission{}, this, [this](const QPermission &permission) {
            refreshMicrophonePermission();
            if (permission.status() == Qt::PermissionStatus::Granted && m_active) {
                startMeter();
            }
        });
    });
    connect(m_openMicrophoneSettings, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QString::fromLatin1(microphonePaneUrl)));
    });
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
                if (state != Qt::ApplicationActive || !m_active || !isVisible()) {
                    return;
                }
                refreshMicrophonePermission();
                if (qApp->checkPermission(QMicrophonePermission{})
                    == Qt::PermissionStatus::Granted) {
                    startMeter();
                }
            });
    refreshMicrophonePermission();
}

void MicrophoneSetupPage::refreshMicrophonePermission()
{
    const Qt::PermissionStatus status = qApp->checkPermission(QMicrophonePermission{});
    m_allowMicrophone->setVisible(status == Qt::PermissionStatus::Undetermined);
    m_openMicrophoneSettings->setVisible(status == Qt::PermissionStatus::Denied);
    switch (status) {
    case Qt::PermissionStatus::Granted:
        m_permissionStatus->setText(QStringLiteral("macOS lets Speecher use the microphone."));
        break;
    case Qt::PermissionStatus::Undetermined:
        m_permissionStatus->setText(
            QStringLiteral("macOS has not been asked yet. Speecher only records while you dictate."));
        break;
    case Qt::PermissionStatus::Denied:
        m_permissionStatus->setText(
            QStringLiteral("Microphone access is off, so Speecher records silence. Turn Speecher on under Privacy & Security > Microphone, then come back to this page."));
        break;
    }
    setStatusColor(m_permissionStatus, status == Qt::PermissionStatus::Granted);
}

void MicrophoneSetupPage::refreshInputVolume()
{
    const std::optional<float> volume = m_platform.inputVolume();
    if (!volume || *volume >= 0.5f) {
        m_inputVolumeStatus->hide();
        return;
    }
    m_inputVolumeStatus->setText(
        QStringLiteral("macOS input volume for the default microphone is at %1% — raise it in System Settings > Sound > Input if Speecher hears you too quietly.")
            .arg(qRound(*volume * 100.0f)));
    m_inputVolumeStatus->show();
}
#endif

void MicrophoneSetupPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
#ifdef Q_OS_MACOS
    // The grant can change in System Settings while this page sits in the
    // wizard, and macOS does not push that back to a running process.
    refreshMicrophonePermission();
#endif
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
    m_input->stop();
    m_level->setValue(0);
#ifdef Q_OS_MACOS
    refreshInputVolume();
#endif
    QString error;
    if (!m_input->start(&error)) {
        m_status->setText(error);
        return;
    }
    m_status->setText(QStringLiteral("Listening for microphone input…"));
}

AccessibilitySetupPage::AccessibilitySetupPage(ApplicationController &controller,
                                               QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_status(new QLabel(this))
#ifdef Q_OS_MACOS
    , m_enable(new QPushButton(QStringLiteral("Grant Accessibility access"), this))
    , m_poll(new QTimer(this))
#else
    , m_enable(new QPushButton(QStringLiteral("Enable permanently"), this))
#endif
{
    QVBoxLayout *layout = makePage(
        this,
#ifdef Q_OS_MACOS
        QStringLiteral("Speecher pastes your dictation into the frontmost app with a synthetic Cmd+V. macOS calls that controlling your computer, so it needs Accessibility permission."));
#else
        QStringLiteral("AT-SPI lets Speecher identify the target app, paste into compatible fields, edit selected text, and learn corrections."));
#endif
    m_status->setWordWrap(true);
    m_enable->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    layout->addWidget(m_status);
#ifdef Q_OS_MACOS
    layout->addWidget(m_enable, 0, Qt::AlignLeft);
    // macOS records the grant against the app signature and never delivers it to
    // the process that asked, so the only way to notice it is to keep looking.
    m_poll->setInterval(1000);
    connect(m_poll, &QTimer::timeout, &m_controller, &ApplicationController::refreshAccessibilityState);
    connect(m_enable, &QPushButton::clicked, this, [this] {
        m_lastError.clear();
        m_controller.platform()->requestAccessibility();
        QString error;
        if (!m_controller.enableAccessibility(&error)) {
            m_lastError = error;
        }
        refreshFromController();
    });
#else
    layout->addWidget(m_enable, 0, Qt::AlignLeft);
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

bool AccessibilitySetupPage::accessibilityGrantAppearedDuringSetup() const
{
#ifdef Q_OS_MACOS
    // The poll that keeps m_controller's cached state fresh stops once this page
    // is hidden, so refresh synchronously before trusting the cache here.
    m_controller.refreshAccessibilityState();
    return m_initialGrantRecorded
        && !m_initialGrant
        && m_controller.accessibilityEnabled();
#else
    return false;
#endif
}

void AccessibilitySetupPage::refreshFromController()
{
    updateState(m_controller.accessibilitySupported(),
                m_controller.accessibilityEnabled(),
                m_controller.accessibilityPersistent());
}

#ifdef Q_OS_MACOS
void AccessibilitySetupPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    m_controller.refreshAccessibilityState();
    if (!m_initialGrantRecorded) {
        m_initialGrant = m_controller.accessibilityEnabled();
        m_initialGrantRecorded = true;
    }
    m_poll->start();
}

void AccessibilitySetupPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_poll->stop();
}
#endif

void AccessibilitySetupPage::updateState(bool supported, bool enabled, bool persistent)
{
    QString status;
#ifdef Q_OS_MACOS
    Q_UNUSED(supported);
    Q_UNUSED(persistent);
    // A grant that pre-dated this wizard run does not trigger a relaunch on
    // accept() (see accessibilityGrantAppearedDuringSetup()), so the copy must
    // not promise one either.
    const bool grantedDuringSetup = m_initialGrantRecorded && !m_initialGrant;
    status = enabled
        ? (grantedDuringSetup
               ? QStringLiteral("Accessibility is granted. Speecher will restart when setup finishes so macOS hands it the permission.")
               : QStringLiteral("Accessibility is granted."))
        : QStringLiteral("Accessibility is off, so Speecher can copy your dictation but not paste it. Grant it below — Speecher restarts itself when setup finishes.");
    m_enable->setEnabled(!enabled);
#else
    if (!supported) {
        status = QStringLiteral("This Speecher build does not include AT-SPI support.");
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
        status = QStringLiteral("Desktop accessibility is currently off.");
        m_enable->setEnabled(true);
        m_enable->setText(QStringLiteral("Enable permanently"));
    }
#endif
    m_status->setText(m_lastError.isEmpty() ? status : m_lastError);
}

TextDeliverySetupPage::TextDeliverySetupPage(SettingsStore &settings, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_status(new QLabel(this))
    , m_setup(new QPushButton(QStringLiteral("Set up virtual keyboard"), this))
    , m_progress(new QProgressBar(this))
    , m_restoreClipboard(new QCheckBox(QStringLiteral("Restore the previous clipboard after verified paste"), this))
    , m_format(new QComboBox(this))
{
    QVBoxLayout *layout = makePage(
        this,
#ifdef Q_OS_MACOS
        QStringLiteral("Speecher puts the finished text on your clipboard and pastes it into the frontmost app with Cmd+V. The paste needs the Accessibility permission from the previous step; without it the text still reaches your clipboard."));
#else
        QStringLiteral("Speecher can set up ydotool for virtual-keyboard paste. Administrator approval is required once; Speecher remains unprivileged while dictating."));
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
    layout->addSpacing(8);
    layout->addLayout(formatRow);
    layout->addWidget(m_restoreClipboard);
    layout->addStretch();

    connect(m_setup, &QPushButton::clicked, this, &TextDeliverySetupPage::runSetup);
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

#ifdef SPEECHER_WITH_YDOTOOL
void TextDeliverySetupPage::refreshStatus()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_settings.ydotoolEnabled());
    const bool needsSignIn = status.state == YdotoolSetupState::NeedsSignOut;
    m_status->setText(needsSignIn
                          ? QStringLiteral("Set up — activates after your next sign-in.")
                          : status.label + QStringLiteral(". ") + status.detail);
    m_setup->setEnabled(!status.ready() && !needsSignIn);
    m_setup->setText(status.ready() ? QStringLiteral("Virtual keyboard ready")
                                    : QStringLiteral("Set up virtual keyboard"));
}

void TextDeliverySetupPage::runSetup()
{
    m_setup->setEnabled(false);
    m_progress->setVisible(true);
    m_status->setText(QStringLiteral("Setting up ydotool…"));
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
                        QStringLiteral("Set up — activates after your next sign-in. The service could not start: %1")
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
        })) {
        m_progress->setVisible(false);
        refreshStatus();
    }
}
#else
// Keyboard paste needs no user-installed helper off Linux, so the page keeps
// only the clipboard controls, which are portable.
void TextDeliverySetupPage::refreshStatus()
{
    m_status->setText(
#ifdef Q_OS_MACOS
        QStringLiteral("Nothing to install — Speecher uses the keyboard paste built into macOS."));
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
    , m_provider(new QComboBox(this))
    , m_fastMode(new QCheckBox(QStringLiteral("Fast mode"), this))
    , m_fastModeHint(new QLabel(this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Refinement can clean up a raw transcript after dictation. The detected provider is selected by default."));
    for (const ProviderDescriptor &provider : providers.refinementProviders()) {
        m_provider->addItem(provider.label, provider.id);
    }
    m_provider->addItem(QStringLiteral("None"), QStringLiteral("none"));
    settings::selectData(m_provider, m_settings.refinementProvider());

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(QStringLiteral("Provider"), this));
    row->addWidget(m_provider, 1);
    layout->addLayout(row);
    m_fastMode->setObjectName(QStringLiteral("refinementFastMode"));
    m_fastModeHint->setWordWrap(true);
    layout->addWidget(m_fastMode);
    layout->addWidget(m_fastModeHint);
    layout->addStretch();
    updateFastModeControl();
    connect(m_provider, &QComboBox::currentIndexChanged, this, [this] {
        m_settings.setRefinementProvider(m_provider->currentData().toString());
        updateFastModeControl();
    });
    connect(m_fastMode, &QCheckBox::toggled, this, [this](bool checked) {
        const QString provider = m_provider->currentData().toString();
        if (provider == QStringLiteral("openai")) {
            m_settings.setOpenAiFastMode(checked);
        } else if (provider == QStringLiteral("anthropic")) {
            m_settings.setAnthropicFastMode(checked);
        }
    });
}

void RefinementSetupPage::updateFastModeControl()
{
    const QString provider = m_provider->currentData().toString();
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

#ifdef Q_OS_MACOS
StartAtLoginSetupPage::StartAtLoginSetupPage(SettingsStore &settings, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_launchAtLogin(new QCheckBox(QStringLiteral("Start Speecher at login"), this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Dictation only works while Speecher is running."));
    m_launchAtLogin->setObjectName(QStringLiteral("launchAtLogin"));
    m_launchAtLogin->setChecked(m_settings.launchAtLogin());
    layout->addWidget(m_launchAtLogin);
    layout->addStretch();
}

void StartAtLoginSetupPage::apply()
{
    m_settings.setLaunchAtLogin(m_launchAtLogin->isChecked());
}
#endif

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

#ifdef Q_OS_MACOS
    if (m_controller.globalShortcutsSupported()) {
        m_createShortcut = new QCheckBox(QStringLiteral("Set up a dictation shortcut"), this);
        m_createShortcut->setChecked(true);
        m_shortcut = new QKeySequenceEdit(this);
        m_shortcut->setKeySequence(QKeySequence(Qt::META | Qt::ALT | Qt::Key_D));
        auto *shortcutRow = new QHBoxLayout;
        shortcutRow->addWidget(m_createShortcut);
        shortcutRow->addWidget(m_shortcut, 1);
        layout->addLayout(shortcutRow);
        connect(m_createShortcut, &QCheckBox::toggled, m_shortcut, &QWidget::setEnabled);
        const auto resetShortcutFailure = [this] {
            m_shortcutFailureAcknowledged = false;
            m_shortcutStatus->setText(shortcutHint());
        };
        connect(m_createShortcut, &QCheckBox::toggled, this, resetShortcutFailure);
        connect(m_shortcut, &QKeySequenceEdit::keySequenceChanged, this, resetShortcutFailure);
        m_shortcutStatus->setText(shortcutHint());
    } else {
        m_shortcutStatus->setText(
            QStringLiteral("Bind speecher toggle in your desktop's keyboard settings."));
    }
#elif defined(Q_OS_LINUX)
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
#ifdef Q_OS_MACOS
    if (!m_shortcut || m_shortcutLoaded) {
        return;
    }
    m_shortcutLoaded = true;
    const QKeySequence existing = m_controller.globalShortcut();
    if (!existing.isEmpty()) {
        m_shortcut->setKeySequence(existing);
    }
#endif
}

#ifdef Q_OS_LINUX
void FinishSetupPage::updateLinuxShortcutInstruction()
{
    const QString display = m_controller.globalShortcutDisplay();
    if (!display.isEmpty()) {
        m_shortcutStatus->setText(
            QStringLiteral("Press %1 to start dictating, speak, then press it again to stop and insert the text.")
                .arg(display));
        m_manualCommand->hide();
        return;
    }
    m_shortcutStatus->setText(
        m_controller.globalShortcutsSupported()
            ? QStringLiteral(
                  "No Global Shortcut is set yet. Go back to set one, or bind this command yourself:")
            : linuxGlobalShortcutManualInstruction());
    m_manualCommand->setText(linuxGlobalShortcutCommand());
    m_manualCommand->show();
}
#endif

void FinishSetupPage::setSignInRequired(bool required)
{
    m_signInNote->setVisible(required);
    m_signInNote->setText(required
                              ? QStringLiteral("Sign out and back in before using virtual-keyboard paste so the new group membership takes effect.")
                              : QString());
}

bool FinishSetupPage::applyShortcut()
{
#ifdef Q_OS_MACOS
    if (!m_createShortcut || !m_createShortcut->isChecked()) {
        return true;
    }
    if (m_shortcutFailureAcknowledged) {
        return true;
    }
    QString error;
    if (!m_controller.setGlobalShortcut(m_shortcut->keySequence(), &error)) {
        m_shortcutFailureAcknowledged = true;
        m_shortcutStatus->setText(shortcutFailureHint().arg(error));
        return false;
    }
    m_shortcutStatus->setText(QStringLiteral("Dictation shortcut registered."));
#endif
    return true;
}

} // namespace speecher
