#include "ui/TranscribeWindow.h"

#include "ui/TranscribePage.h"

#include <QVBoxLayout>

namespace speecher {

TranscribeWindow::TranscribeWindow(ApplicationController *controller, QWidget *parent)
    : QWidget(parent, Qt::Window)
    , m_page(new TranscribePage(controller, this))
{
    setObjectName(QStringLiteral("transcribeWindow"));
    setWindowTitle(QStringLiteral("Transcribe — Speecher"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_page);
    resize(560, 680);
}

TranscribePage *TranscribeWindow::page() const
{
    return m_page;
}

} // namespace speecher
