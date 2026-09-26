#include "ui/TranscribeWindow.h"

#include "ui/TranscribePage.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QStyle>
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
    // One card column with the page's margins beside it, and tall enough
    // for the setup form without scrolling at the default font.
    const int margins = style()->pixelMetric(QStyle::PM_LayoutLeftMargin, nullptr, m_page)
        + style()->pixelMetric(QStyle::PM_LayoutRightMargin, nullptr, m_page);
    resize(settings::cardMaximumWidth() + margins + settings::largeSpacing(), settings::gridUnit() * 38);
}

TranscribePage *TranscribeWindow::page() const
{
    return m_page;
}

} // namespace speecher
