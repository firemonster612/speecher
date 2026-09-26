#pragma once

#include <QWidget>

namespace speecher {

class ApplicationController;
class TranscribePage;

// The window a file manager's "Open with" lands in: the Transcribe page on
// its own, without the sidebar and settings of the main window.
class TranscribeWindow : public QWidget {
    Q_OBJECT

public:
    explicit TranscribeWindow(ApplicationController *controller, QWidget *parent = nullptr);

    TranscribePage *page() const;

private:
    TranscribePage *m_page;
};

} // namespace speecher
