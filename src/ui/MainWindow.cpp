#include "ui/MainWindow.h"

#include "app/ApplicationController.h"
#include "ui/SettingsDialog.h"

#include <QPushButton>
#include <QVBoxLayout>

namespace speecher {

MainWindow::MainWindow(ApplicationController *controller, QWidget *parent)
    : QMainWindow(parent)
    , m_controller(controller)
    , m_toggle(new QPushButton(QStringLiteral("Start"), this))
{
    setWindowTitle(QStringLiteral("Speecher"));
    resize(260, 132);
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *settings = new QPushButton(QStringLiteral("Settings"), this);
    m_toggle->setMinimumHeight(48);
    settings->setMinimumHeight(40);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    layout->addWidget(m_toggle);
    layout->addWidget(settings);
    setCentralWidget(central);

    connect(m_toggle, &QPushButton::clicked, m_controller, &ApplicationController::toggle);
    connect(settings, &QPushButton::clicked, this, [this] {
        SettingsDialog dialog(m_controller, this);
        dialog.exec();
    });
}

void MainWindow::setStatusText(const QString &status)
{
    const QString state = status.toCaseFolded();
    const bool canStop = state == QStringLiteral("starting") || state == QStringLiteral("listening");
    m_toggle->setText(canStop ? QStringLiteral("Stop") : QStringLiteral("Start"));
}

} // namespace speecher
