#include "output/YdotoolSetupFlow.h"

#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#include "output/YdotoolDelivery.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <memory>

namespace speecher {
namespace {

bool setupInProgress = false;

bool confirmYdotoolEnable(QWidget *parent)
{
    const std::unique_ptr<QDialog> dialog(createYdotoolEnableDialog(
        parent,
        [](const QString &text, QString *error) {
            YdotoolDelivery ydotool;
            return ydotool.type(text, error);
        }));
    return dialog->exec() == QDialog::Accepted;
}

} // namespace

QDialog *createYdotoolEnableDialog(
    QWidget *parent,
    std::function<bool(const QString &text, QString *error)> typeText)
{
    auto *dialog = new QDialog(parent);
    dialog->setWindowTitle(QStringLiteral("Enable virtual keyboard"));
    auto *layout = new QVBoxLayout(dialog);
    auto *label = new QLabel(
        QStringLiteral("Run the typing test with the field below focused. Once the test "
                       "passes, choose Enable to turn on virtual keyboard paste."),
        dialog);
    label->setWordWrap(true);
    auto *field = new QLineEdit(dialog);
    field->setClearButtonEnabled(true);
    auto *status = new QLabel(dialog);
    status->setWordWrap(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
    auto *run = buttons->addButton(QStringLiteral("Run test"), QDialogButtonBox::ActionRole);
    run->setObjectName(QStringLiteral("ydotoolRunTest"));
    auto *enable = buttons->addButton(QStringLiteral("Enable"), QDialogButtonBox::AcceptRole);
    enable->setObjectName(QStringLiteral("ydotoolEnable"));
    enable->setEnabled(false);
    layout->addWidget(label);
    layout->addWidget(field);
    layout->addWidget(status);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(run, &QPushButton::clicked, dialog, [field, status, enable, dialog, typeText] {
        field->clear();
        field->setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(150, field, [field, status, enable, dialog, typeText] {
            QString error;
            const QString expected = QStringLiteral("speecher test");
            if (!typeText(expected, &error)) {
                QMessageBox::warning(
                    dialog,
                    QStringLiteral("Virtual keyboard test failed"),
                    error);
                return;
            }
            QTimer::singleShot(350, field, [field, status, enable, dialog, expected] {
                if (field->text() != expected) {
                    QMessageBox::warning(
                        dialog,
                        QStringLiteral("Virtual keyboard test failed"),
                        QStringLiteral("The test field did not receive the expected text."));
                    return;
                }
                status->setText(QStringLiteral("Test passed. Choose Enable to finish."));
                enable->setEnabled(true);
                enable->setDefault(true);
            });
        });
    });
    dialog->resize(420, dialog->sizeHint().height());
    field->setFocus(Qt::OtherFocusReason);
    return dialog;
}

bool startYdotoolSetup(SettingsStore &settings,
                       QWidget *dialogParent,
                       YdotoolSetupFlowOptions options,
                       QObject *callbackContext,
                       std::function<void(const YdotoolSetupFlowResult &)> finished)
{
    if (setupInProgress) {
        return false;
    }
    setupInProgress = true;

    const YdotoolSetupStatus current = YdotoolSetup::probe(settings.ydotoolEnabled());
    if (current.state == YdotoolSetupState::Disabled
        && current.speecherManagedSetupInstalled) {
        YdotoolSetupFlowResult result;
        result.helperOk = true;
        result.status = current;
        if (confirmYdotoolEnable(dialogParent)) {
            settings.setYdotoolEnabled(true);
        }
        setupInProgress = false;
        finished(result);
        return true;
    }

    if (options.confirmInstall
        && QMessageBox::question(
               dialogParent,
               QStringLiteral("Set up virtual keyboard"),
               QStringLiteral("Speecher will copy its setup helper to your local libexec directory. The system authentication dialog will show its full path and ask for administrator permission to install ydotool if needed, load uinput, configure a speecher-uinput group, install udev rules, and install a user-level ydotoold service. Speecher itself remains unprivileged while dictating."),
               QMessageBox::Cancel | QMessageBox::Ok,
               QMessageBox::Ok)
            != QMessageBox::Ok) {
        setupInProgress = false;
        return false;
    }

    const auto result = std::make_shared<YdotoolSetupFlowResult>();
    const QPointer<QWidget> parentGuard(dialogParent);
    const QPointer<QObject> callbackGuard(callbackContext);
    QThread *thread = QThread::create([result] {
        result->helperOk = YdotoolSetup::runHelper(
            YdotoolSetup::HelperAction::Install,
            &result->helperError);
        if (result->helperOk) {
            YdotoolSetup::startUserService(&result->serviceError);
        }
    });
    QObject::connect(thread, &QThread::finished, &settings, [&settings, options, result, parentGuard, callbackGuard, finished = std::move(finished)] {
        if (result->helperOk) {
            result->status = YdotoolSetup::probe(true);
            if (options.applyAutomaticOutputMethod) {
                settings.setOutputMethod(QString::fromLatin1(OutputMethod::Automatic));
            }
            // Enabling is the user's explicit step: no Enable click, no
            // enable. In the sign-out case the test cannot run yet, so the
            // enable step waits until after the next sign-in (the settings
            // row then offers Enable).
            if (result->status.ready()
                && parentGuard && confirmYdotoolEnable(parentGuard)) {
                settings.setYdotoolEnabled(true);
            }
        }
        setupInProgress = false;
        if (callbackGuard) {
            finished(*result);
        }
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return true;
}

} // namespace speecher
