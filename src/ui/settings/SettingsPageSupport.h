#pragma once

#include "core/settings/SettingsSchema.h"

#include <QColor>
#include <QFont>
#include <QLabel>
#include <QMargins>
#include <QString>

class QColor;
class QComboBox;
class QFrame;
class QGridLayout;
class QPushButton;
class QFormLayout;
class QLabel;
class QLayout;
class QListWidget;
class QPalette;
class QProgressBar;
class QScrollArea;
class QToolButton;
class QVBoxLayout;
class QWidget;

namespace speecher {
struct AudioInputDeviceInfo;
}

namespace speecher::settings {

// A word-wrapped QLabel that needs more lines than its width-blind size hint
// paints its last line clipped; keeping minimumHeight at heightForWidth makes
// the layout give it the real height (same fix as the settings rows').
class WrappingLabel final : public QLabel {
public:
    explicit WrappingLabel(const QString &text = {}, QWidget *parent = nullptr);
    explicit WrappingLabel(QWidget *parent);

protected:
    void resizeEvent(QResizeEvent *event) override;
};

// One rating: the style's progress bar with its value after it, "8.5/10", or
// "?" with no bar. Bar and value keep their widths either way, so the cells of
// neighbouring rows line up.
class RatingCell final : public QWidget {
public:
    explicit RatingCell(QWidget *parent = nullptr);
    void setRating(const Rating &rating);

private:
    QProgressBar *m_bar;
    QLabel *m_value;
};

// A provider's bars, each after the name of what it measures: side by side
// under a setup option, stacked on a Settings row.
class RatingBars final : public QWidget {
public:
    explicit RatingBars(Qt::Orientation orientation, QWidget *parent = nullptr);
    void setRatings(const QList<Rating> &ratings);

private:
    struct Bar {
        QLabel *measure = nullptr;
        RatingCell *cell = nullptr;
    };
    Qt::Orientation m_orientation;
    QGridLayout *m_grid;
    QList<Bar> m_bars;
};

// The models behind a provider, in a card: each name with its note under it,
// and, where the models have bars, a column of them per measure under a header.
class RatedModelList final : public QWidget {
public:
    explicit RatedModelList(QWidget *parent = nullptr);
    void setModels(const QList<RatedModel> &models);

private:
    QFormLayout *m_rows;
    QList<RatedModel> m_models;
};

// Qt Widgets has no disclosure, so a tool button with an arrow stands in, as
// in the setup assistant: it shows and hides content, which starts hidden,
// and points right while closed and down while open.
QToolButton *makeDisclosure(const QString &caption, QWidget *content, QWidget *parent);

QFrame *makeSeparator(QWidget *parent);
QColor separatorColor(const QPalette &palette);
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
// Fills combo with options, each closed one disabled with its help as the
// tooltip. Rebuilding a combo resets its selection, so one that already
// offers the same choices is left alone: the caller selects the value
// straight after.
void setOptions(QComboBox *combo, const QList<RowOption> &options);
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
// Fills a CLI Proxy API account picker from the account files in directory.
// The Providers settings page and the setup assistant share the rules: an
// explicit choice when several accounts exist, expired/missing markers, and a
// disabled placeholder when the directory holds nothing.
void populateCliproxyAccounts(QComboBox *combo,
                              const QString &directory,
                              const QString &type,
                              const QString &selected);
QColor positiveTextColor(const QPalette &palette);
// The colour scheme's NegativeText, or plain text without a scheme.
QColor negativeTextColor(const QPalette &palette);
// Shows a row's description, or a status in its place, in the colour scheme's
// NegativeText while negative, else in the description's grey. The colour is
// copied from palette, so call it again when the palette changes.
void setDescriptionTone(QLabel *description, bool negative, const QPalette &palette);
// The colour scheme's NeutralText, the caution colour a paused dictation
// shows in; plain text without a scheme.
QColor neutralTextColor(const QPalette &palette);
QLabel *makeSectionLabel(const QString &text, QWidget *parent);
// The bold a section title is set in, on pages and in the sidebar's headers.
QFont sectionTitleFont(const QFont &font);
QFrame *makeSettingsCard(QWidget *parent);
// FormButtonDelegate: the whole row is the button, with a trailing arrow. With
// dynamicDescription an empty description is kept (hidden) for the caller to
// fill in later.
QPushButton *makeButtonRow(const QString &title,
                           const QString &description,
                           QWidget *parent,
                           bool dynamicDescription = false);
// Updates a button row's visible title and accessible name; QPushButton::text
// is never drawn on these rows.
void setButtonRowCaption(QPushButton *row, const QString &caption);
// Kirigami Addons FormCard metrics.
int gridUnit();
int smallSpacing();
int largeSpacing();
int cornerRadius();
int cardMaximumWidth();
// Widest a page column grows on a large pane.
int cardStretchedWidth();
QMargins rowPadding();
QFont smallFont(const QFont &font);
QColor frameColor(const QPalette &palette);
QFormLayout *cardFormLayout(QWidget *card);
// Adds a spanning row to a card, with a hairline above every row after the first.
void addCardRow(QFormLayout *layout, QWidget *row, QWidget *parent);
// Shows or hides a row of a card along with the hairline above it, which would
// otherwise be left behind as a gap where the row was.
void setCardRowVisible(QWidget *row, bool visible);
// Wraps a section (title + card) in the shared, centred content column.
QWidget *centerColumn(QWidget *content, QWidget *parent);
void addSectionRow(QFormLayout *form, const QString &title, QWidget *parent);
// Configures the shared page container; makeSettingsPage and AppWindow both use it.
void configurePageScroll(QScrollArea *scroll, QWidget *content);
QVBoxLayout *makeSettingsPage(QScrollArea *scroll);

} // namespace speecher::settings
