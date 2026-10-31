#include "app/VirtualKeyboardSetup.h"

#include "core/OutputMethod.h"
#include "core/SettingsStore.h"
#ifdef Q_OS_LINUX
#include "output/YdotoolDelivery.h"
#endif

#include <QThread>

#include <memory>
#include <utility>

namespace speecher {

VirtualKeyboardSetup::System VirtualKeyboardSetup::onThisComputer()
{
#ifdef Q_OS_LINUX
    return {
        .probe = YdotoolSetup::probe,
        .runHelper = YdotoolSetup::runHelper,
        .startService = YdotoolSetup::startUserService,
        .stopService = YdotoolSetup::stopUserService,
        .type = [](const QString &text, QString *error) { return YdotoolDelivery().type(text, error); },
    };
#else
    const auto fail = [](auto &&...) { return false; };
    return {
        .probe = [](bool) { return YdotoolSetupStatus{}; },
        .runHelper = fail,
        .startService = fail,
        .stopService = fail,
        .type = fail,
    };
#endif
}

VirtualKeyboardSetup::VirtualKeyboardSetup(SettingsStore *settings, System system, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_system(std::move(system))
{
    m_arrival.setSingleShot(true);
    m_arrival.setInterval(testArrivalMs);
    connect(&m_arrival, &QTimer::timeout, this, [this] { setTest(Test::Missed); });
    m_status = m_system.probe(m_settings->dictationSnapshot().output.ydotoolEnabled);
}

VirtualKeyboardSetup::~VirtualKeyboardSetup() = default;

QString VirtualKeyboardSetup::testText()
{
    return QStringLiteral("speecher test");
}

void VirtualKeyboardSetup::refresh()
{
    const YdotoolSetupStatus status = m_system.probe(m_settings->dictationSnapshot().output.ydotoolEnabled);
    if (status.state == m_status.state && status.speecherManagedSetupInstalled == m_status.speecherManagedSetupInstalled) {
        return;
    }
    m_status = status;
    emit changed();
}

void VirtualKeyboardSetup::setUp(AfterSetUp after)
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
        [this, result, after] {
            if (!result->installed) {
                emit failed(Failure::SetUp, result->helperError);
                return;
            }
            if (after == AfterSetUp::PasteAutomatically) {
                m_settings->edit([](AppSettings &settings) {
                    settings.output.method = QString::fromLatin1(OutputMethod::Automatic);
                    return true;
                });
            }
            if (!result->serviceError.isEmpty()) {
                emit failed(Failure::Start, result->serviceError);
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
                emit failed(Failure::Start, *error);
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
            emit failed(Failure::Type, *error);
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
            // to take away. Files another account still uses are not left:
            // the probe counts them only while this account is in the group.
            if (!result->stopError.isEmpty()) {
                emit failed(Failure::Stop, result->stopError);
            }
            if (!result->removed) {
                emit failed(Failure::Remove, result->helperError);
            } else if (m_system.probe(false).speecherManagedSetupInstalled) {
                emit failed(Failure::FilesLeft, QString());
            } else {
                m_settings->setYdotoolEnabled(false);
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
        m_status = m_system.probe(m_settings->dictationSnapshot().output.ydotoolEnabled);
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
