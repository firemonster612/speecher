#pragma once

#include "core/settings/SettingsSchema.h"

#include <QColor>
#include <QString>

class QComboBox;
class QAbstractItemView;
class QLabel;
class QLayout;
class QPalette;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace speecher {
struct AudioInputDeviceInfo;
}

namespace speecher::settings {

QColor separatorColor(const QPalette &palette);
void selectData(QComboBox *combo, const QString &data);
void selectEditableText(QComboBox *combo, const QString &text);
QString editableComboValue(const QComboBox *combo);
void setComboItemEnabled(QComboBox *combo,
                         int index,
                         bool enabled,
                         const QString &toolTip = QString());

// The spacing scale shared by the settings form-card surfaces.
int tightSpacing();
int relatedSpacing();
int groupGap();
int sectionGap();
// The height of a line of the application font, which is what the row
// padding and the control widths are measured in.
int gridUnit();
int rowHorizontalPadding();
int rowVerticalPadding();
// Cards stretch to this and no further, centred in whatever is left.
int cardMaximumWidth();
// Combo boxes, spin boxes and line edits in a row's control column start at
// this width, so short values still line up down the card.
int controlMinimumWidth();
// A read-only value on the right wraps rather than growing past this.
int valueMaximumWidth();
int collectionEditorMinimumHeight(const QAbstractItemView *view, int visibleRows);

// True while the KDE platform theme is drawing this process, which is the only
// time kdeglobals colours match the palette the rest of the window uses.
bool kdePlatformThemeActive();
QPalette kdeHeaderPalette(const QPalette &base);
// The header strip's palette: KDE's header colours under the KDE platform
// theme, otherwise a shade of the active palette's window colour.
QPalette headerPalette(const QPalette &base);
void applyPageMargins(QLayout *layout);
QLabel *makePageTitle(const QString &text, QWidget *parent);
QList<RowOption> audioInputDeviceOptions(const QList<AudioInputDeviceInfo> &devices);
void populateAudioInputDevices(QComboBox *combo,
                               const QList<AudioInputDeviceInfo> &devices,
                               const QString &selectedDeviceId);
QColor positiveTextColor(const QPalette &palette);
// Turns a scroll area into a settings page: frameless, window-coloured, with
// a resizable content widget whose layout this returns.
QVBoxLayout *makeSettingsPage(QScrollArea *scroll);

} // namespace speecher::settings
