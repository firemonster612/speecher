#include <QApplication>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QWidget>

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("speecher-atspi-fixture"));

    QWidget window;
    window.setWindowTitle(QStringLiteral("Speecher AT-SPI Fixture"));
    auto *layout = new QVBoxLayout(&window);
    auto *target = new QLineEdit(QStringLiteral("prefix text suffix text"), &window);
    target->setObjectName(QStringLiteral("savedTarget"));
    target->setCursorPosition(12);
    layout->addWidget(target);

    window.resize(480, 100);
    window.show();
    target->setFocus();
    return app.exec();
}
