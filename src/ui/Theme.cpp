#include "ui/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QWidget>

namespace speecher::Theme {

static QPalette lightPalette()
{
    QPalette p = qApp && qApp->style() ? qApp->style()->standardPalette() : QPalette();
    p.setColor(QPalette::Window, QColor(250, 250, 250));
    p.setColor(QPalette::WindowText, QColor(18, 18, 18));
    p.setColor(QPalette::Base, QColor(255, 255, 255));
    p.setColor(QPalette::AlternateBase, QColor(244, 244, 244));
    p.setColor(QPalette::Text, QColor(18, 18, 18));
    p.setColor(QPalette::Button, QColor(248, 248, 248));
    p.setColor(QPalette::ButtonText, QColor(18, 18, 18));
    p.setColor(QPalette::BrightText, QColor(255, 255, 255));
    p.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    p.setColor(QPalette::ToolTipText, QColor(18, 18, 18));
    p.setColor(QPalette::PlaceholderText, QColor(110, 110, 110));
    p.setColor(QPalette::Light, QColor(255, 255, 255));
    p.setColor(QPalette::Midlight, QColor(232, 232, 232));
    p.setColor(QPalette::Mid, QColor(196, 196, 196));
    p.setColor(QPalette::Dark, QColor(150, 150, 150));
    p.setColor(QPalette::Shadow, QColor(90, 90, 90));
    p.setColor(QPalette::Highlight, QColor(210, 55, 86));
    p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    p.setColor(QPalette::Link, QColor(30, 92, 210));
    return p;
}

static QPalette darkPalette()
{
    QPalette p = qApp && qApp->style() ? qApp->style()->standardPalette() : QPalette();
    p.setColor(QPalette::Window, QColor(31, 31, 31));
    p.setColor(QPalette::WindowText, QColor(245, 245, 245));
    p.setColor(QPalette::Base, QColor(39, 39, 39));
    p.setColor(QPalette::AlternateBase, QColor(33, 33, 33));
    p.setColor(QPalette::Text, QColor(245, 245, 245));
    p.setColor(QPalette::Button, QColor(43, 43, 43));
    p.setColor(QPalette::ButtonText, QColor(245, 245, 245));
    p.setColor(QPalette::BrightText, QColor(255, 255, 255));
    p.setColor(QPalette::ToolTipBase, QColor(43, 43, 43));
    p.setColor(QPalette::ToolTipText, QColor(245, 245, 245));
    p.setColor(QPalette::PlaceholderText, QColor(145, 145, 145));
    p.setColor(QPalette::Light, QColor(82, 82, 82));
    p.setColor(QPalette::Midlight, QColor(55, 55, 55));
    p.setColor(QPalette::Mid, QColor(74, 74, 74));
    p.setColor(QPalette::Dark, QColor(22, 22, 22));
    p.setColor(QPalette::Shadow, QColor(10, 10, 10));
    p.setColor(QPalette::Highlight, QColor(235, 58, 92));
    p.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    p.setColor(QPalette::Link, QColor(115, 165, 255));
    return p;
}

static void repolishWidgets()
{
    for (QWidget *widget : QApplication::topLevelWidgets()) {
        const QList<QWidget *> widgets = widget->findChildren<QWidget *>();
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
        for (QWidget *child : widgets) {
            child->style()->unpolish(child);
            child->style()->polish(child);
            child->update();
        }
    }
}

void apply(const QString &theme)
{
    if (!qApp) {
        return;
    }
    if (theme == QStringLiteral("light")) {
        qApp->setPalette(lightPalette());
        repolishWidgets();
        return;
    }
    if (theme == QStringLiteral("dark")) {
        qApp->setPalette(darkPalette());
        repolishWidgets();
        return;
    }
    qApp->setPalette(qApp->style()->standardPalette());
    repolishWidgets();
}

} // namespace speecher::Theme
