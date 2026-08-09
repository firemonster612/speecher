#pragma once

#include <QColor>
#include <QString>

class QComboBox;
class QFrame;
class QFormLayout;
class QLabel;
class QLayout;
class QPalette;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace speecher::settings {

QFrame *makeSeparator(QWidget *parent);
QColor separatorColor(const QPalette &palette);
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
QPalette kdeHeaderPalette(const QPalette &base);
void applyPageMargins(QLayout *layout);
void applyLabelHierarchy(QWidget *root);
QLabel *makePageTitle(const QString &text, QWidget *parent);
QLabel *makeSectionLabel(const QString &text, QWidget *parent);
QFrame *makeSettingsCard(QWidget *parent);
QVBoxLayout *makeSettingsPage(QScrollArea *scroll);

} // namespace speecher::settings
