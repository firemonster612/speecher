#include "ui/setup/SetupPages.h"

#include "app/ApplicationController.h"
#include "app/LinuxComposition.h"
#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "dictation/DictationPorts.h"
#include "output/YdotoolSetup.h"
#include "providers/ClaudeCredentials.h"
#include "providers/ProviderRegistry.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPalette>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QThread>
#include <QVBoxLayout>

#include <memory>

namespace speecher {
namespace {

QVBoxLayout *makePage(QWidget *page, const QString &title, const QString &description)
{
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    auto *heading = new QLabel(title, page);
    QFont font = heading->font();
    font.setPointSize(font.pointSize() + 4);
    font.setBold(true);
    heading->setFont(font);
    layout->addWidget(heading);

    auto *intro = new QLabel(description, page);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    return layout;
}

void setStatusColor(QLabel *label, bool positive)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText,
                     positive ? QColor(35, 135, 65)
                              : label->parentWidget()->palette().color(QPalette::WindowText));
    label->setPalette(palette);
}

void addProfiles(QComboBox *combo)
{
    combo->addItem(QStringLiteral("Work"), QStringLiteral("work"));
    combo->addItem(QStringLiteral("Email"), QStringLiteral("email"));
    combo->addItem(QStringLiteral("Personal"), QStringLiteral("personal"));
    combo->addItem(QStringLiteral("Other"), QStringLiteral("other"));
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
    }
    return QStringLiteral("Other");
}

} // namespace

WelcomeSetupPage::WelcomeSetupPage(QWidget *parent)
    : QWidget(parent)
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Welcome to Speecher"),
        QStringLiteral("Speecher records a short dictation, turns it into text, and sends it to the app you were using."));
    auto *checks = new QLabel(
        QStringLiteral("This assistant checks your Claude sign-in, microphone, desktop accessibility, text delivery, refinement, and writing profiles."),
        this);
    checks->setWordWrap(true);
    layout->addWidget(checks);
    layout->addStretch();
}

ClaudeSignInSetupPage::ClaudeSignInSetupPage(SettingsStore &settings, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_status(new QLabel(this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Claude sign-in"),
        QStringLiteral("Claude Voice is required for transcription. Speecher uses the Claude Code OAuth session stored in ~/.claude/.credentials.json."));
    m_status->setWordWrap(true);
    auto *instructions = new QLabel(
        QStringLiteral("If sign-in is missing or expired, open a terminal and run `claude /login` or `claude auth login`, then check again."),
        this);
    instructions->setWordWrap(true);
    auto *checkAgain = new QPushButton(QStringLiteral("Check again"), this);
    checkAgain->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    layout->addWidget(m_status);
    layout->addWidget(instructions);
    layout->addWidget(checkAgain, 0, Qt::AlignLeft);
    layout->addStretch();
    connect(checkAgain, &QPushButton::clicked, this, &ClaudeSignInSetupPage::checkCredentials);
    checkCredentials();
}

void ClaudeSignInSetupPage::checkCredentials()
{
    const ClaudeCredentialResult result = ClaudeCredentials::load(m_settings.claudeCredentialsPath());
    setStatusColor(m_status, result.ok);
    m_status->setText(result.ok
                          ? QStringLiteral("Signed in to Claude Code. Credentials are valid.")
                          : result.error);
}

MicrophoneSetupPage::MicrophoneSetupPage(SettingsStore &settings,
                                         const LinuxComposition &platform,
                                         QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_platform(platform)
    , m_device(new QComboBox(this))
    , m_level(new QProgressBar(this))
    , m_status(new QLabel(this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Microphone"),
        QStringLiteral("Choose the input Speecher should record. Speak normally and check that the level moves."));
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

    refreshDevices();
    m_input = m_platform.createAudioInput(&m_settings, this);
    connect(m_input, &AudioInput::levelChanged, this, [this](float level) {
        m_level->setValue(qBound(0, qRound(level * 100.0f), 100));
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
        refreshDevices();
        startMeter();
    } else if (m_input) {
        m_input->stop();
        m_level->setValue(0);
    }
}

void MicrophoneSetupPage::refreshDevices()
{
    const QString selected = m_settings.audioInputDeviceId();
    const QSignalBlocker blocker(m_device);
    m_device->clear();
    m_device->addItem(QStringLiteral("System default"), QString());
    for (const AudioInputDeviceInfo &device : m_platform.availableAudioInputDevices()) {
        m_device->addItem(device.isDefault
                              ? QStringLiteral("%1 (default)").arg(device.label)
                              : device.label,
                          device.id);
    }
    if (!selected.isEmpty() && m_device->findData(selected) < 0) {
        m_device->addItem(QStringLiteral("Missing microphone"), selected);
        settings::setComboItemEnabled(m_device,
                                      m_device->count() - 1,
                                      false,
                                      QStringLiteral("This saved microphone is not currently available."));
    }
    settings::selectData(m_device, selected);
}

void MicrophoneSetupPage::startMeter()
{
    m_input->stop();
    m_level->setValue(0);
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
    , m_enable(new QPushButton(QStringLiteral("Enable permanently"), this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Desktop accessibility"),
        QStringLiteral("AT-SPI lets Speecher identify the target app, paste into compatible fields, edit selected text, and learn corrections."));
    m_status->setWordWrap(true);
    m_enable->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    layout->addWidget(m_status);
    layout->addWidget(m_enable, 0, Qt::AlignLeft);
    layout->addStretch();

    connect(m_enable, &QPushButton::clicked, this, [this] {
        QString error;
        if (!m_controller.enableAccessibility(&error)) {
            m_status->setText(error);
        }
    });
    connect(&m_controller,
            &ApplicationController::accessibilityStateChanged,
            this,
            &AccessibilitySetupPage::updateState);
    updateState(m_controller.accessibilitySupported(),
                m_controller.accessibilityEnabled(),
                m_controller.accessibilityPersistent());
}

void AccessibilitySetupPage::updateState(bool supported, bool enabled, bool persistent)
{
    if (!supported) {
        m_status->setText(QStringLiteral("This Speecher build does not include AT-SPI support."));
        m_enable->setEnabled(false);
        m_enable->setText(QStringLiteral("Unavailable"));
    } else if (enabled && persistent) {
        m_status->setText(QStringLiteral("Desktop accessibility is enabled permanently."));
        m_enable->setEnabled(false);
        m_enable->setText(QStringLiteral("Enabled"));
    } else if (enabled) {
        m_status->setText(QStringLiteral("Desktop accessibility is enabled for this session only."));
        m_enable->setEnabled(true);
        m_enable->setText(QStringLiteral("Enable permanently"));
    } else {
        m_status->setText(QStringLiteral("Desktop accessibility is currently off."));
        m_enable->setEnabled(true);
        m_enable->setText(QStringLiteral("Enable permanently"));
    }
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
        QStringLiteral("Text delivery"),
        QStringLiteral("Speecher can set up ydotool for virtual-keyboard paste. Administrator approval is required once; Speecher remains unprivileged while dictating."));
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
    return m_needsSignIn;
}

void TextDeliverySetupPage::refreshStatus()
{
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_settings.ydotoolEnabled());
    m_needsSignIn = status.state == YdotoolSetupState::NeedsSignOut;
    m_status->setText(m_needsSignIn
                          ? QStringLiteral("Set up — activates after your next sign-in.")
                          : status.label + QStringLiteral(". ") + status.detail);
    m_setup->setEnabled(!status.ready() && !m_needsSignIn);
    m_setup->setText(status.ready() ? QStringLiteral("Virtual keyboard ready")
                                    : QStringLiteral("Set up virtual keyboard"));
}

void TextDeliverySetupPage::runSetup()
{
    struct SetupResult {
        bool helperOk = false;
        QString helperError;
        QString serviceError;
    };
    const auto result = std::make_shared<SetupResult>();
    m_setup->setEnabled(false);
    m_progress->setVisible(true);
    m_status->setText(QStringLiteral("Setting up ydotool…"));

    QThread *thread = QThread::create([result] {
        result->helperOk = YdotoolSetup::runHelper(
            YdotoolSetup::HelperAction::Install,
            &result->helperError);
        if (result->helperOk) {
            YdotoolSetup::startUserService(&result->serviceError);
        }
    });
    connect(thread, &QThread::finished, this, [this, result] {
        m_progress->setVisible(false);
        if (!result->helperOk) {
            m_status->setText(QStringLiteral("Setup failed: %1").arg(result->helperError));
            m_setup->setEnabled(true);
            return;
        }

        const YdotoolSetupStatus status = YdotoolSetup::probe(true);
        m_settings.setOutputMethod(QString::fromLatin1(OutputMethod::Automatic));
        if (status.ready() || status.state == YdotoolSetupState::NeedsSignOut) {
            m_settings.setYdotoolEnabled(true);
        }
        refreshStatus();
        if (!result->serviceError.isEmpty()
            && status.state != YdotoolSetupState::NeedsSignOut
            && !status.ready()) {
            m_status->setText(QStringLiteral("Setup installed, but the service could not start: %1")
                                  .arg(result->serviceError));
            m_setup->setEnabled(true);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

RefinementSetupPage::RefinementSetupPage(SettingsStore &settings,
                                         ProviderRegistry &providers,
                                         QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_provider(new QComboBox(this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Refinement"),
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
    layout->addStretch();
    connect(m_provider, &QComboBox::currentIndexChanged, this, [this] {
        m_settings.setRefinementProvider(m_provider->currentData().toString());
    });
}

WritingProfilesSetupPage::WritingProfilesSetupPage(SettingsStore &settings, QWidget *parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_defaultProfile(new QComboBox(this))
{
    QVBoxLayout *layout = makePage(
        this,
        QStringLiteral("Writing profiles"),
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
        QStringLiteral("Ready to dictate"),
        QStringLiteral("Put the cursor in a text field, trigger dictation, speak, then trigger it again to stop and insert the transcript."));
    m_shortcutStatus->setWordWrap(true);
    m_signInNote->setWordWrap(true);

    if (m_controller.globalShortcutsSupported()) {
        m_createShortcut = new QCheckBox(QStringLiteral("Set up a dictation shortcut"), this);
        m_createShortcut->setChecked(true);
        m_shortcut = new QKeySequenceEdit(this);
        const QKeySequence existing = m_controller.globalShortcut();
        m_shortcut->setKeySequence(existing.isEmpty()
                                       ? QKeySequence(Qt::META | Qt::ALT | Qt::Key_D)
                                       : existing);
        auto *shortcutRow = new QHBoxLayout;
        shortcutRow->addWidget(m_createShortcut);
        shortcutRow->addWidget(m_shortcut, 1);
        layout->addLayout(shortcutRow);
        connect(m_createShortcut, &QCheckBox::toggled, m_shortcut, &QWidget::setEnabled);
        m_shortcutStatus->setText(QStringLiteral("The shortcut triggers `speecher toggle`."));
    } else {
        m_shortcutStatus->setText(
            QStringLiteral("Bind `speecher toggle` in your desktop environment's global shortcut settings."));
    }
    layout->addWidget(m_shortcutStatus);
    layout->addWidget(m_signInNote);
    layout->addStretch();
}

void FinishSetupPage::setSignInRequired(bool required)
{
    m_signInNote->setVisible(required);
    m_signInNote->setText(required
                              ? QStringLiteral("Sign out and back in before using virtual-keyboard paste so the new group membership takes effect.")
                              : QString());
}

void FinishSetupPage::applyShortcut()
{
    if (!m_createShortcut || !m_createShortcut->isChecked()) {
        return;
    }
    QString error;
    if (!m_controller.setGlobalShortcut(m_shortcut->keySequence(), &error)) {
        m_shortcutStatus->setText(QStringLiteral("Could not register the shortcut: %1").arg(error));
    }
}

} // namespace speecher
