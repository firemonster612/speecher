#pragma once

#include "core/settings/SettingsSchema.h"

#include <QColor>
#include <QString>

class QColor;
class QComboBox;
class QFrame;
class QFormLayout;
class QLabel;
class QLayout;
class QListWidget;
class QPalette;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace speecher {
struct AudioInputDeviceInfo;
}

namespace speecher::settings {

QFrame *makeSeparator(QWidget *parent);
QColor separatorColor(const QPalette &palette);
QWidget *makeCenteredSeparator(QWidget *parent);
void configureFormLayout(QFormLayout *form);
QFrame *makeRow(const QString &label,
                const QString &description,
                QWidget *control,
                QWidget *parent,
                QWidget *titleAccessory = nullptr,
                bool dynamicDescription = false);
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
// True while the KDE platform theme is drawing this process, which is the only
// time kdeglobals colours match the palette the rest of the window uses.
bool kdePlatformThemeActive();
QPalette kdeHeaderPalette(const QPalette &base);
// The header strip's palette: KDE's header colours under the KDE platform
// theme, otherwise a shade of the active palette's window colour.
QPalette headerPalette(const QPalette &base);
void applyPageMargins(QLayout *layout);
void applyLabelHierarchy(QWidget *root);
QLabel *makePageTitle(const QString &text, QWidget *parent);
QList<RowOption> audioInputDeviceOptions(const QList<AudioInputDeviceInfo> &devices);
void populateAudioInputDevices(QComboBox *combo,
                               const QList<AudioInputDeviceInfo> &devices,
                               const QString &selectedDeviceId);
QColor positiveTextColor(const QPalette &palette);
QLabel *makeSectionLabel(const QString &text, QWidget *parent);
QFrame *makeSettingsCard(QWidget *parent);
QFormLayout *cardFormLayout(QFrame *card);
void addSectionRow(QFormLayout *form, const QString &title, QWidget *parent);
QVBoxLayout *makeSettingsPage(QScrollArea *scroll);

} // namespace speecher::settings
