#include "ui/MainWindow.h"

#include "app/ApplicationController.h"
#include "ui/AccessibilityNotice.h"

#include <QPushButton>
#include <QVBoxLayout>

namespace speecher {

MainWindow::MainWindow(ApplicationController *controller, QWidget *parent)
    : QMainWindow(parent)
    , m_controller(controller)
    , m_toggle(new QPushButton(QStringLiteral("Start"), this))
    , m_accessibilityNotice(new AccessibilityNotice(this))
{
    setWindowTitle(QStringLiteral("Speecher"));
    resize(260, 132);
    m_accessibilityNotice->setCompact(true);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *settings = new QPushButton(QStringLiteral("Settings"), this);
    m_toggle->setMinimumHeight(48);
    settings->setMinimumHeight(40);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    layout->addWidget(m_accessibilityNotice);
    layout->addWidget(m_toggle);
    layout->addWidget(settings);
    setCentralWidget(central);

    connect(m_toggle, &QPushButton::clicked, m_controller, &ApplicationController::toggle);
    connect(settings, &QPushButton::clicked, m_controller, &ApplicationController::showSettings);
    connect(m_accessibilityNotice, &AccessibilityNotice::enableRequested, this, [this] {
        QString error;
        if (!m_controller->enableAccessibility(&error)) {
            m_accessibilityNotice->showError(error);
        }
    });
    connect(m_controller,
            &ApplicationController::accessibilityStateChanged,
            m_accessibilityNotice,
            qOverload<bool, bool, bool>(&AccessibilityNotice::setState));
    m_accessibilityNotice->setState(m_controller->accessibilitySupported(),
                                    m_controller->accessibilityEnabled(),
                                    m_controller->accessibilityPersistent());
}

void MainWindow::setStatusText(const QString &status)
{
    const QString state = status.toCaseFolded();
    const bool canStop = state == QStringLiteral("starting") || state == QStringLiteral("listening");
    m_toggle->setText(canStop ? QStringLiteral("Stop") : QStringLiteral("Start"));
}

} // namespace speecher
