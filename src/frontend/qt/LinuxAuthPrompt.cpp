#include "frontend/qt/LinuxAuthPrompt.h"

#include "output/HelperInstall.h"

#include <QApplication>
#include <QInputDialog>
#include <QThread>

#ifdef SPEECHER_WITH_KASSISTANT
#include <KPasswordDialog>
#endif

namespace speecher {
namespace {

// pkexec's own prompt is usually just "Password:", which names neither the
// app asking nor why.
QString promptTitle()
{
    return QStringLiteral("Speecher needs permission");
}

QString promptPurpose()
{
    return QStringLiteral("Speecher needs your administrator password to finish setting up a "
                          "system component, such as the virtual keyboard.");
}

bool askOnGuiThread(const QString &promptText, bool echoOff, QString *reply)
{
#ifdef SPEECHER_WITH_KASSISTANT
    if (echoOff) {
        KPasswordDialog dialog(QApplication::activeWindow());
        dialog.setWindowTitle(promptTitle());
        dialog.setPrompt(promptPurpose());
        if (dialog.exec() != QDialog::Accepted) {
            return false;
        }
        *reply = dialog.password();
        return true;
    }
#endif
    bool accepted = false;
    const QString answer = QInputDialog::getText(
        QApplication::activeWindow(),
        promptTitle(),
        promptPurpose() + QStringLiteral("\n\n") + promptText.trimmed(),
        echoOff ? QLineEdit::Password : QLineEdit::Normal,
        QString(),
        &accepted);
    if (accepted) {
        *reply = answer;
    }
    return accepted;
}

} // namespace

void installLinuxAuthPrompt()
{
    helpers::setPkexecPrompt([](const QString &promptText, bool echoOff, QString *reply) {
        // The helpers run on worker threads; the dialog belongs to the GUI
        // thread, which waits nowhere while setup runs, so block until it
        // answers.
        if (QThread::currentThread() == qApp->thread()) {
            return askOnGuiThread(promptText, echoOff, reply);
        }
        bool accepted = false;
        QMetaObject::invokeMethod(
            qApp,
            [&] { accepted = askOnGuiThread(promptText, echoOff, reply); },
            Qt::BlockingQueuedConnection);
        return accepted;
    });
}

} // namespace speecher
