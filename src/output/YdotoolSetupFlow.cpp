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

bool verifyYdotoolTyping(QWidget *parent)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("Verify ydotool"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *label = new QLabel(
        QStringLiteral("Keep this field focused while Speecher tests virtual keyboard input."),
        &dialog);
    label->setWordWrap(true);
    auto *field = new QLineEdit(&dialog);
    field->setClearButtonEnabled(true);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, &dialog);
    auto *run = buttons->addButton(QStringLiteral("Run test"), QDialogButtonBox::AcceptRole);
    layout->addWidget(label);
    layout->addWidget(field);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(run, &QPushButton::clicked, &dialog, [field, &dialog] {
        field->clear();
        field->setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(150, field, [field, &dialog] {
            QString error;
            YdotoolDelivery ydotool;
            const QString expected = QStringLiteral("speecher test");
            if (!ydotool.type(expected, &error)) {
                QMessageBox::warning(
                    &dialog,
                    QStringLiteral("ydotool verification failed"),
                    error);
                return;
            }
            QTimer::singleShot(350, field, [field, expected, &dialog] {
                if (field->text() == expected) {
                    dialog.accept();
                } else {
                    QMessageBox::warning(
                        &dialog,
                        QStringLiteral("ydotool verification failed"),
                        QStringLiteral("The test field did not receive the expected text."));
                }
            });
        });
    });
    dialog.resize(420, dialog.sizeHint().height());
    field->setFocus(Qt::OtherFocusReason);
    return dialog.exec() == QDialog::Accepted;
}

} // namespace

bool startYdotoolSetup(SettingsStore &settings,
                       QWidget *dialogParent,
                       bool confirmInstall,
                       QObject *callbackContext,
                       std::function<void(const YdotoolSetupFlowResult &)> finished)
{
    const YdotoolSetupStatus current = YdotoolSetup::probe(settings.ydotoolEnabled());
    if (current.state == YdotoolSetupState::Disabled
        && current.speecherManagedSetupInstalled) {
        YdotoolSetupFlowResult result;
        result.helperOk = true;
        result.status = current;
        if (verifyYdotoolTyping(dialogParent)) {
            settings.setYdotoolEnabled(true);
        }
        finished(result);
        return true;
    }

    if (confirmInstall
        && QMessageBox::question(
               dialogParent,
               QStringLiteral("Set up virtual keyboard"),
               QStringLiteral("Speecher will ask for administrator permission to install ydotool if needed, load uinput, configure a speecher-uinput group, install udev rules, and install a user-level ydotoold service. Speecher itself remains unprivileged at runtime."),
               QMessageBox::Cancel | QMessageBox::Ok,
               QMessageBox::Ok)
            != QMessageBox::Ok) {
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
    QObject::connect(thread, &QThread::finished, &settings, [&, result, parentGuard, callbackGuard, finished = std::move(finished)] {
        if (result->helperOk) {
            result->status = YdotoolSetup::probe(true);
            settings.setOutputMethod(QString::fromLatin1(OutputMethod::Automatic));
            if (result->status.state == YdotoolSetupState::NeedsSignOut
                || (result->status.ready()
                    && (!parentGuard || verifyYdotoolTyping(parentGuard)))) {
                settings.setYdotoolEnabled(true);
            }
        }
        if (callbackGuard) {
            finished(*result);
        }
    });
    QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
    return true;
}

} // namespace speecher
