#pragma once

#include <QList>
#include <QPair>
#include <QString>

class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLayout;
class QListWidget;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class QWidget;

namespace speecher::settings {

QFrame *makeSeparator(QWidget *parent);
QFrame *makeRow(const QString &label,
                const QString &description,
                QWidget *control,
                QWidget *parent,
                QWidget *titleAccessory = nullptr);
void addRow(QVBoxLayout *layout,
            QFrame *row,
            QWidget *parent,
            bool addSeparator = true);
void selectData(QComboBox *combo, const QString &data);
void selectEditableText(QComboBox *combo, const QString &text);
QString editableComboValue(const QComboBox *combo);
void setComboItemEnabled(QComboBox *combo,
                         int index,
                         bool enabled,
                         const QString &toolTip = QString());
void applyPageMargins(QLayout *layout);
void applyLabelHierarchy(QWidget *root);
QLabel *makeSectionLabel(const QString &text, QWidget *parent);
QFrame *makeSettingsCard(QWidget *parent);
QVBoxLayout *makeSettingsPage(QScrollArea *scroll);
void addPageContainer(QHBoxLayout *layout,
                      const QList<QPair<QString, QString>> &categories,
                      const QList<QWidget *> &pages,
                      QListWidget **categoriesWidget,
                      QStackedWidget **pagesWidget,
                      QWidget *parent);

} // namespace speecher::settings
