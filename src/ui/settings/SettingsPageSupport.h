#pragma once

#include <QList>
#include <QPair>
#include <QString>

class QComboBox;
class QFrame;
class QFormLayout;
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
QWidget *makeCenteredSeparator(QWidget *parent);
void configureFormLayout(QFormLayout *form);
QFrame *makeRow(const QString &label,
                const QString &description,
                QWidget *control,
                QWidget *parent,
                QWidget *titleAccessory = nullptr);
void addRow(QFormLayout *layout,
            QFrame *row,
            QWidget *parent,
            bool addSeparator = false);
void selectData(QComboBox *combo, const QString &data);
void selectEditableText(QComboBox *combo, const QString &text);
QString editableComboValue(const QComboBox *combo);
void setComboItemEnabled(QComboBox *combo,
                         int index,
                         bool enabled,
                         const QString &toolTip = QString());
int tightSpacing();
int relatedSpacing();
int groupGap();
int sectionGap();
void applyPageMargins(QLayout *layout);
void applyLabelHierarchy(QWidget *root);
QLabel *makePageTitle(const QString &text, QWidget *parent);
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
