#include "frontend/qt/OutputCustomRows.h"

#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#ifdef SPEECHER_WITH_YDOTOOL
#include "output/YdotoolSetup.h"
#include "output/YdotoolSetupFlow.h"
#endif
#include "ui/settings/SettingsPageSupport.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QThread>

#include <memory>
#include <utility>

namespace speecher {

namespace {

void runSetupWork(QObject *context,
                  std::function<void()> work,
                  std::function<void()> finished)
{
    QThread *thread = QThread::create(std::move(work));
    QObject::connect(thread, &QThread::finished, context, std::move(finished));
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void updateWrappedHeight(QLabel *label, int width)
{
    label->setMinimumHeight(0);
    label->setMinimumHeight(label->heightForWidth(width));
}

class WrappedStatusLabel final : public QLabel {
public:
    using QLabel::QLabel;

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        updateWrappedHeight(this, event->size().width());
    }
};

#ifdef SPEECHER_WITH_YDOTOOL
void setWrappedText(QLabel *label, const QString &text)
{
    label->setText(text);
    updateWrappedHeight(label, label->width());
}
#endif

} // namespace

OutputCustomRows::OutputCustomRows(SettingsStore &settings)
    : m_settings(settings)
{
}

SchemaCustomRowFactory OutputCustomRows::factory()
{
    return [this](const SettingsRow &descriptor,
                  QWidget *parent,
                  std::function<void()> notifyChanged) {
        if (descriptor.id == QStringLiteral("outputMethod")) {
            return makeMethodRow(parent, std::move(notifyChanged));
        }
        if (descriptor.id == QStringLiteral("virtualKeyboard")) {
            return makeVirtualKeyboardRow(parent, std::move(notifyChanged));
        }
        return SchemaCustomRow{};
    };
}

SchemaCustomRow OutputCustomRows::makeMethodRow(QWidget *parent, std::function<void()> notifyChanged)
{
    m_method = new QComboBox(parent);
    m_notifyChanged = notifyChanged;
    const auto addMethod = [this](const char *method) {
        m_method->addItem(OutputMethod::label(QString::fromLatin1(method)),
                          QString::fromLatin1(method));
    };
    addMethod(OutputMethod::Automatic);
    addMethod(OutputMethod::DirectInsert);
#ifdef Q_OS_MACOS
    addMethod(OutputMethod::MacPaste);
#else
    addMethod(OutputMethod::Ydotool);
    addMethod(OutputMethod::WlCopy);
#endif
    addMethod(OutputMethod::QtClipboard);
    m_method->setToolTip(QStringLiteral("How Speecher delivers final text."));
    m_method->view()->setMouseTracking(true);

    QObject::connect(m_method, &QComboBox::currentIndexChanged, m_method, [this, notifyChanged] {
        // The user has now chosen for themselves, so there is nothing left to
        // preserve.
        m_unlistedMethod.clear();
#ifdef SPEECHER_WITH_YDOTOOL
        // The item is disabled (and says so) while the virtual keyboard is not
        // set up, so this only guards a programmatic selection.
        if (m_method->currentData().toString() == QString::fromLatin1(OutputMethod::Ydotool)) {
            const YdotoolSetupStatus status = YdotoolSetup::probe(m_settings.ydotoolEnabled());
            if (!status.ready() || !m_settings.ydotoolEnabled()) {
                const QSignalBlocker blocker(m_method);
                settings::selectData(m_method, m_settings.outputMethod());
                return;
            }
        }
#endif
        notifyChanged();
    });

    return {
        m_method,
        [this] {
            return QVariant(m_unlistedMethod.isEmpty() ? m_method->currentData().toString()
                                                       : m_unlistedMethod);
        },
        [this](const QVariant &value) {
            // selectData falls back to index 0, which is Automatic on every
            // platform. Remember what it could not show so the draft does not
            // quietly rewrite a method this build simply has no item for.
            const QString method = value.toString();
            const bool listed = m_method->findData(method) >= 0;
            settings::selectData(m_method, method);
            m_unlistedMethod = listed ? QString() : method;
        },
    };
}

SchemaCustomRow OutputCustomRows::makeVirtualKeyboardRow(QWidget *parent,
                                                         std::function<void()> notifyChanged)
{
    m_notifyChanged = std::move(notifyChanged);
    // The status reads under the row's title; the buttons are the row's
    // control.
    m_status = new WrappedStatusLabel(parent);
    m_status->setObjectName(QStringLiteral("statusText"));
    m_status->setWordWrap(true);
    m_status->setForegroundRole(QPalette::WindowText);
    m_status->setAttribute(Qt::WA_StyledBackground, false);

    auto *control = new QWidget(parent);
    m_setUp = new QPushButton(QStringLiteral("Set up"), control);
    m_start = new QPushButton(QStringLiteral("Start service"), control);
    m_disable = new QPushButton(QStringLiteral("Disable in Speecher"), control);
    m_remove = new QPushButton(QStringLiteral("Remove setup"), control);
    auto *buttons = new QHBoxLayout(control);
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(settings::relatedSpacing());
    for (QPushButton *button : {m_setUp, m_start, m_disable, m_remove}) {
        buttons->addWidget(button);
    }

#ifdef SPEECHER_WITH_YDOTOOL
    QObject::connect(m_setUp, &QPushButton::clicked, control, [this] { setUpOrEnable(); });
    QObject::connect(m_start, &QPushButton::clicked, control, [this] {
        m_start->setEnabled(false);
        const auto error = std::make_shared<QString>();
        runSetupWork(
            m_status,
            [error] { YdotoolSetup::startUserService(error.get()); },
            [this, error] {
                if (!error->isEmpty()) {
                    QMessageBox::warning(m_status, QStringLiteral("Virtual keyboard service"), *error);
                }
                refresh();
            });
    });
    QObject::connect(m_disable, &QPushButton::clicked, control, [this] { disable(); });
    QObject::connect(m_remove, &QPushButton::clicked, control, [this] { removeSetup(); });
#endif
    updateButtons();
    return {control, {}, {}, false, m_status};
}

#ifdef SPEECHER_WITH_YDOTOOL
void OutputCustomRows::refresh()
{
    if (!m_method) {
        return;
    }
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_settings.ydotoolEnabled());
    const bool enabled = m_settings.ydotoolEnabled() && status.ready();
    const int index = m_method->findData(QString::fromLatin1(OutputMethod::Ydotool));
    // The choice says in its own text why it cannot be picked; a hover tooltip
    // on a disabled item is not something everyone sees.
    const QString pasteLabel = OutputMethod::label(QString::fromLatin1(OutputMethod::Ydotool));
    m_method->setItemText(index,
                          enabled ? pasteLabel
                                  : QStringLiteral("%1 (not set up)").arg(pasteLabel));
    settings::setComboItemEnabled(
        m_method,
        index,
        enabled,
        enabled ? QString() : QStringLiteral("Set up the virtual keyboard below first."));
    const bool unavailableSelection = !enabled
        && m_method->currentData().toString() == QString::fromLatin1(OutputMethod::Ydotool);
    m_method->setToolTip(
        unavailableSelection
            ? QStringLiteral("The virtual keyboard is not set up yet, so text is copied to the clipboard instead.")
            : enabled
                ? QStringLiteral("Automatic pastes with the virtual keyboard and falls back to the clipboard.")
                : QStringLiteral("Automatic copies to the clipboard. Set up the virtual keyboard below to paste as well."));
    if (m_status) {
        setWrappedText(m_status, status.label + QStringLiteral(". ") + status.detail);
    }
    updateButtons();
}

void OutputCustomRows::updateButtons()
{
    if (!m_setUp) {
        return;
    }
    const YdotoolSetupStatus status = YdotoolSetup::probe(m_settings.ydotoolEnabled());
    const bool ready = status.ready();
    const bool disabled = status.state == YdotoolSetupState::Disabled;
    const bool daemonMissing = status.state == YdotoolSetupState::DaemonNotRunning;
    const bool setupInstalled = status.speecherManagedSetupInstalled || ready || disabled;
    const QString setUpFirst = QStringLiteral("Run setup first");
    m_setUp->setText(disabled && status.speecherManagedSetupInstalled ? QStringLiteral("Enable")
                                                                     : QStringLiteral("Set up"));
    m_setUp->setVisible(!ready || disabled);
    m_setUp->setEnabled(status.state != YdotoolSetupState::NeedsSignOut);
    m_start->setVisible(daemonMissing);
    m_start->setEnabled(setupInstalled);
    m_start->setToolTip(setupInstalled ? QString() : setUpFirst);
    m_disable->setVisible(ready && m_settings.ydotoolEnabled());
    m_remove->setVisible(status.speecherManagedSetupInstalled);
    m_remove->setEnabled(status.speecherManagedSetupInstalled);
    m_remove->setToolTip(setupInstalled ? QString() : setUpFirst);
}

void OutputCustomRows::setUpOrEnable()
{
    m_setUp->setEnabled(false);
    if (!startYdotoolSetup(
            m_settings,
            m_status,
            YdotoolSetupFlowOptions{
                .confirmInstall = true,
                .applyAutomaticOutputMethod = false,
            },
            m_status,
            [this](const YdotoolSetupFlowResult &result) {
                if (!result.helperOk) {
                    QMessageBox::warning(m_status,
                                         QStringLiteral("Virtual keyboard setup failed"),
                                         result.helperError);
                } else if (!result.serviceError.isEmpty()) {
                    QMessageBox::warning(m_status,
                                         QStringLiteral("Virtual keyboard service"),
                                         result.serviceError);
                }
                refresh();
                m_notifyChanged();
            })) {
        refresh();
    }
}

void OutputCustomRows::disable()
{
    m_settings.setYdotoolEnabled(false);
    const QSignalBlocker blocker(m_method);
    settings::selectData(m_method, m_settings.outputMethod());
    refresh();
    m_notifyChanged();
}

void OutputCustomRows::removeSetup()
{
    const int answer = QMessageBox::question(
        m_status,
        QStringLiteral("Remove virtual keyboard setup"),
        QStringLiteral("Speecher will ask for administrator permission to remove the service, the "
                       "device rule, the module-load file and the group membership it set up. The "
                       "ydotool package your distribution installed stays."),
        QMessageBox::Cancel | QMessageBox::Ok,
        QMessageBox::Cancel);
    if (answer != QMessageBox::Ok) {
        return;
    }
    m_remove->setEnabled(false);
    struct RemovalResult {
        bool helperOk = false;
        QString helperError;
        QString stopError;
        YdotoolSetupStatus status;
    };
    const auto result = std::make_shared<RemovalResult>();
    runSetupWork(
        m_status,
        [result] {
            YdotoolSetup::stopUserService(&result->stopError);
            result->helperOk = YdotoolSetup::runHelper(
                YdotoolSetup::HelperAction::Remove, &result->helperError);
            result->status = YdotoolSetup::probe(false);
        },
        [this, result] {
            if (!result->helperOk) {
                QMessageBox::warning(
                    m_status, QStringLiteral("Virtual keyboard removal failed"), result->helperError);
            } else if (result->status.speecherManagedSetupInstalled) {
                QMessageBox::warning(
                    m_status,
                    QStringLiteral("Virtual keyboard removal incomplete"),
                    QStringLiteral("The removal finished, but some of the files Speecher set up are still there."));
            } else {
                m_settings.setYdotoolEnabled(false);
                const QSignalBlocker blocker(m_method);
                settings::selectData(m_method, m_settings.outputMethod());
                m_notifyChanged();
            }
            refresh();
        });
}
#else
// ydotool is Linux-only, and so is the Advanced section that hosts its cluster:
// the schema leaves the section out, so only the method row exists here.
void OutputCustomRows::refresh()
{
    if (m_method) {
        m_method->setToolTip(
#ifdef Q_OS_MACOS
            QStringLiteral("Automatic pastes with Cmd+V, then falls back to the clipboard."));
#else
            QStringLiteral("Automatic copies to the clipboard; this build has no virtual keyboard to paste with."));
#endif
    }
}

void OutputCustomRows::updateButtons() {}

void OutputCustomRows::setUpOrEnable() {}

void OutputCustomRows::disable() {}

void OutputCustomRows::removeSetup() {}
#endif

} // namespace speecher
