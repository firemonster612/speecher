#include "app/VirtualKeyboardSetup.h"

#include "core/SettingsStore.h"
#include "output/YdotoolDelivery.h"

#include <QThread>

#include <memory>
#include <utility>

namespace speecher {

bool VirtualKeyboardSetup::typeWithYdotool(const QString &text, QString *error)
{
    YdotoolDelivery ydotool;
    return ydotool.type(text, error);
}

VirtualKeyboardSetup::VirtualKeyboardSetup(SettingsStore *settings, QObject *parent)
    : VirtualKeyboardSetup(settings, System{}, parent)
{
}

VirtualKeyboardSetup::VirtualKeyboardSetup(SettingsStore *settings, System system, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_system(std::move(system))
{
    m_arrival.setSingleShot(true);
    m_arrival.setInterval(testArrivalMs);
    connect(&m_arrival, &QTimer::timeout, this, [this] { setTest(Test::Missed); });
    m_status = m_system.probe(m_settings->ydotoolEnabled());
}

VirtualKeyboardSetup::~VirtualKeyboardSetup() = default;

QString VirtualKeyboardSetup::testText()
{
    return QStringLiteral("speecher test");
}

void VirtualKeyboardSetup::refresh()
{
    const YdotoolSetupStatus status = m_system.probe(m_settings->ydotoolEnabled());
    if (status.state == m_status.state && status.speecherManagedSetupInstalled == m_status.speecherManagedSetupInstalled) {
        return;
    }
    m_status = status;
    emit changed();
}

void VirtualKeyboardSetup::setUp()
{
    if (m_operation != Operation::None) {
        return;
    }
    // Installed and only off in Speecher: nothing privileged is left to do.
    if (m_status.state == YdotoolSetupState::Disabled && m_status.speecherManagedSetupInstalled) {
        beginTest();
        return;
    }
    struct Result {
        bool installed = false;
        QString helperError;
        QString serviceError;
    };
    const auto result = std::make_shared<Result>();
    runOffThread(
        Operation::SettingUp,
        [system = m_system, result] {
            result->installed = system.runHelper(YdotoolSetup::HelperAction::Install, &result->helperError);
            if (result->installed) {
                system.startService(&result->serviceError);
            }
        },
        [this, result] {
            if (!result->installed) {
                emit failed(result->helperError.isEmpty() ? QStringLiteral("The virtual keyboard setup failed.")
                                                          : result->helperError);
                return;
            }
            if (!result->serviceError.isEmpty()) {
                emit failed(result->serviceError);
            }
            // In the sign-out case the test can't run yet; the next sign-in
            // offers it.
            if (m_system.probe(true).ready()) {
                beginTest();
            }
        });
}

void VirtualKeyboardSetup::startService()
{
    if (m_operation != Operation::None) {
        return;
    }
    const auto error = std::make_shared<QString>();
    runOffThread(
        Operation::Starting,
        [system = m_system, error] { system.startService(error.get()); },
        [this, error] {
            if (!error->isEmpty()) {
                emit failed(*error);
            }
        });
}

void VirtualKeyboardSetup::beginTest()
{
    setTest(Test::Ready);
}

void VirtualKeyboardSetup::runTest()
{
    if (m_test == Test::Off || m_test == Test::Typing) {
        return;
    }
    setTest(Test::Typing);
    const auto error = std::make_shared<QString>();
    const auto typed = std::make_shared<bool>(false);
    // Typing is not an Operation: the status stays as it was meanwhile.
    QThread *thread = QThread::create([type = m_system.type, error, typed] {
        *typed = type(testText(), error.get());
    });
    connect(thread, &QThread::finished, this, [this, error, typed] {
        if (m_test != Test::Typing) {
            return;
        }
        if (!*typed) {
            setTest(Test::Ready);
            emit failed(error->isEmpty() ? QStringLiteral("The virtual keyboard could not type.") : *error);
            return;
        }
        m_arrival.start();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VirtualKeyboardSetup::receivedTestText(const QString &text)
{
    if (m_test != Test::Typing || text != testText()) {
        return;
    }
    m_settings->setYdotoolEnabled(true);
    m_status = m_system.probe(true);
    setTest(Test::Off);
}

void VirtualKeyboardSetup::cancelTest()
{
    setTest(Test::Off);
}

void VirtualKeyboardSetup::disable()
{
    m_settings->setYdotoolEnabled(false);
    m_status = m_system.probe(false);
    emit changed();
}

void VirtualKeyboardSetup::remove()
{
    if (m_operation != Operation::None) {
        return;
    }
    struct Result {
        QString stopError;
        bool removed = false;
        QString helperError;
    };
    const auto result = std::make_shared<Result>();
    setTest(Test::Off);
    runOffThread(
        Operation::Removing,
        [system = m_system, result] {
            system.stopService(&result->stopError);
            result->removed = system.runHelper(YdotoolSetup::HelperAction::Remove, &result->helperError);
        },
        [this, result] {
            // Everything that did not come off is named: a service still
            // running, or files the helper left, is access the person asked
            // to take away.
            QStringList problems;
            if (!result->stopError.isEmpty()) {
                problems.append(
                    QStringLiteral("The virtual keyboard service could not be stopped: %1").arg(result->stopError));
            }
            if (!result->removed) {
                problems.append(result->helperError.isEmpty()
                                    ? QStringLiteral("The virtual keyboard setup could not be removed.")
                                    : result->helperError);
            } else if (m_system.probe(false).speecherManagedSetupInstalled) {
                problems.append(
                    QStringLiteral("The removal finished, but some of the files Speecher set up are still there."));
            } else {
                m_settings->setYdotoolEnabled(false);
            }
            if (!problems.isEmpty()) {
                emit failed(problems.join(QLatin1Char(' ')));
            }
        });
}

void VirtualKeyboardSetup::runOffThread(Operation operation, std::function<void()> work, std::function<void()> done)
{
    m_operation = operation;
    emit changed();
    QThread *thread = QThread::create(std::move(work));
    connect(thread, &QThread::finished, this, [this, done = std::move(done)] {
        m_operation = Operation::None;
        done();
        m_status = m_system.probe(m_settings->ydotoolEnabled());
        emit changed();
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void VirtualKeyboardSetup::setTest(Test test)
{
    if (test != Test::Typing) {
        m_arrival.stop();
    }
    if (m_test == test) {
        return;
    }
    m_test = test;
    emit changed();
}

} // namespace speecher
