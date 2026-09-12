#include "frontend/qt/LinuxAuthPrompt.h"

#include "output/HelperInstall.h"

#include <QApplication>
#include <QInputDialog>
#include <QThread>

namespace speecher {
namespace {

bool askOnGuiThread(const QString &promptText, bool echoOff, QString *reply)
{
    bool accepted = false;
    const QString answer = QInputDialog::getText(
        QApplication::activeWindow(),
        QStringLiteral("Authentication required"),
        promptText.trimmed(),
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
