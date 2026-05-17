#pragma once

#include <QMainWindow>

class QPushButton;

namespace speecher {

class ApplicationController;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(ApplicationController *controller, QWidget *parent = nullptr);

public slots:
    void setStatusText(const QString &status);

private:
    ApplicationController *m_controller = nullptr;
    QPushButton *m_toggle = nullptr;
};

} // namespace speecher
