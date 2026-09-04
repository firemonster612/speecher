#include "ui/settings/SettingsPageSupport.h"

#include "dictation/DictationPorts.h"

#include <QApplication>
#include <QComboBox>
#include <QFile>
#include <QFontMetrics>
#include <QFrame>
#include <QLabel>
#include <QPalette>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyle>
#include <QTextStream>
#include <QVBoxLayout>

#ifdef SPEECHER_WITH_KCOLORSCHEME
#include <KColorScheme>
#endif

namespace speecher::settings {

namespace {
// Plasma's own settings cards stop growing at about this width; wider rows put
// the control too far from its title to read as one line.
constexpr int kCardMaximumWidth = 680;
} // namespace

QColor separatorColor(const QPalette &palette)
{
    const QColor window = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
    return QColor((window.red() * 3 + text.red()) / 4,
                  (window.green() * 3 + text.green()) / 4,
                  (window.blue() * 3 + text.blue()) / 4);
}

void selectData(QComboBox *combo, const QString &data)
{
    const int index = combo->findData(data);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

void selectEditableText(QComboBox *combo, const QString &text)
{
    const QString trimmed = text.trimmed();
    const int dataIndex = combo->findData(trimmed);
    if (dataIndex >= 0) {
        combo->setCurrentIndex(dataIndex);
        return;
    }
    const int index = combo->findText(trimmed);
    if (index >= 0) {
        combo->setCurrentIndex(index);
        return;
    }
    combo->addItem(trimmed, trimmed);
    combo->setCurrentIndex(combo->count() - 1);
}

QString editableComboValue(const QComboBox *combo)
{
    const int index = combo->currentIndex();
    const QString text = combo->currentText().trimmed();
    if (index >= 0 && text == combo->itemText(index)) {
        const QString data = combo->itemData(index).toString().trimmed();
        if (!data.isEmpty()) {
            return data;
        }
    }
    return text;
}

void setComboItemEnabled(QComboBox *combo, int index, bool enabled, const QString &toolTip)
{
    auto *model = qobject_cast<QStandardItemModel *>(combo->model());
    if (!model || index < 0) {
        return;
    }
    QStandardItem *item = model->item(index);
    if (!item) {
        return;
    }
    item->setEnabled(enabled);
    item->setToolTip(toolTip);
}

int tightSpacing() { return 4; }
int relatedSpacing() { return 8; }
int groupGap() { return 18; }
int sectionGap() { return 24; }
int gridUnit() { return QFontMetrics(QApplication::font()).height(); }
int rowHorizontalPadding() { return gridUnit(); }
int rowVerticalPadding() { return relatedSpacing() + tightSpacing(); }
int cardMaximumWidth() { return kCardMaximumWidth; }
int controlMinimumWidth() { return QFontMetrics(QApplication::font()).averageCharWidth() * 18; }
int valueMaximumWidth() { return QFontMetrics(QApplication::font()).averageCharWidth() * 40; }

// kdeglobals is a KConfig file, not valid QSettings INI: subgroup section
// lines like "[Colors:Header][Inactive]" derail QSettings' parser and its
// group lookup. Scan for the exact section header line instead.
static QColor kdeGlobalsColor(const QString &section, const QString &key)
{
    QFile file(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
               + QStringLiteral("/kdeglobals"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    const QString wantedHeader = QLatin1Char('[') + section + QLatin1Char(']');
    bool inSection = false;
    QTextStream stream(&file);
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inSection = line == wantedHeader;
            continue;
        }
        if (!inSection || !line.startsWith(key + QLatin1Char('='))) {
            continue;
        }
        const QStringList channels =
            line.mid(key.size() + 1).split(QLatin1Char(','));
        if (channels.size() != 3) {
            return {};
        }
        int rgb[3];
        for (int index = 0; index < 3; ++index) {
            bool valid = false;
            rgb[index] = channels.at(index).trimmed().toInt(&valid);
            if (!valid || rgb[index] < 0 || rgb[index] > 255) {
                return {};
            }
        }
        return QColor(rgb[0], rgb[1], rgb[2]);
    }
    return {};
}

bool kdePlatformThemeActive()
{
    // plasma-integration publishes the scheme it loaded on the application;
    // no other platform theme sets this property.
    if (qApp && qApp->property("KDE_COLOR_SCHEME_PATH").isValid()) {
        return true;
    }
    return qEnvironmentVariable("QT_QPA_PLATFORMTHEME")
               .compare(QStringLiteral("kde"), Qt::CaseInsensitive)
        == 0;
}

QPalette headerPalette(const QPalette &base)
{
    if (kdePlatformThemeActive()) {
        return kdeHeaderPalette(base);
    }
    QPalette result(base);
    const QColor shade = base.color(QPalette::Active, QPalette::Window).darker(110);
    result.setColor(QPalette::Active, QPalette::Window, shade);
    result.setColor(QPalette::Inactive, QPalette::Window, shade);
    result.setColor(QPalette::Disabled, QPalette::Window, shade);
    return result;
}

QPalette kdeHeaderPalette(const QPalette &base)
{
    QPalette result(base);
    QColor background = kdeGlobalsColor(QStringLiteral("Colors:Header"),
                                        QStringLiteral("BackgroundNormal"));
    QColor foreground = kdeGlobalsColor(QStringLiteral("Colors:Header"),
                                        QStringLiteral("ForegroundNormal"));
    QColor inactiveBackground = kdeGlobalsColor(
        QStringLiteral("Colors:Header][Inactive"), QStringLiteral("BackgroundNormal"));
    QColor inactiveForeground = kdeGlobalsColor(
        QStringLiteral("Colors:Header][Inactive"), QStringLiteral("ForegroundNormal"));
    if (!background.isValid()) {
        // Schemes that don't inline a Header group still carry the titlebar
        // color under [WM] — the strip must match the titlebar.
        background = kdeGlobalsColor(QStringLiteral("WM"),
                                     QStringLiteral("activeBackground"));
        foreground = kdeGlobalsColor(QStringLiteral("WM"),
                                     QStringLiteral("activeForeground"));
        inactiveBackground = kdeGlobalsColor(QStringLiteral("WM"),
                                             QStringLiteral("inactiveBackground"));
        inactiveForeground = kdeGlobalsColor(QStringLiteral("WM"),
                                             QStringLiteral("inactiveForeground"));
    }
    result.setColor(QPalette::Active, QPalette::Window,
                    background.isValid() ? background
                                         : base.color(QPalette::Active, QPalette::Window).darker(110));
    result.setColor(QPalette::Active, QPalette::WindowText,
                    foreground.isValid() ? foreground
                                         : base.color(QPalette::Active, QPalette::WindowText));
    const QColor resolvedInactiveBackground =
        inactiveBackground.isValid() ? inactiveBackground : result.color(QPalette::Active, QPalette::Window);
    const QColor resolvedInactiveForeground =
        inactiveForeground.isValid() ? inactiveForeground : result.color(QPalette::Active, QPalette::WindowText);
    result.setColor(QPalette::Inactive, QPalette::Window, resolvedInactiveBackground);
    result.setColor(QPalette::Inactive, QPalette::WindowText, resolvedInactiveForeground);
    result.setColor(QPalette::Disabled, QPalette::Window, resolvedInactiveBackground);
    result.setColor(QPalette::Disabled, QPalette::WindowText, resolvedInactiveForeground);
    return result;
}

void applyPageMargins(QLayout *layout)
{
    QWidget *widget = layout->parentWidget();
    QStyle *style = widget ? widget->style() : QApplication::style();
    layout->setContentsMargins(
        style->pixelMetric(QStyle::PM_LayoutLeftMargin, nullptr, widget),
        style->pixelMetric(QStyle::PM_LayoutTopMargin, nullptr, widget),
        style->pixelMetric(QStyle::PM_LayoutRightMargin, nullptr, widget),
        style->pixelMetric(QStyle::PM_LayoutBottomMargin, nullptr, widget));
}

QList<RowOption> audioInputDeviceOptions(const QList<AudioInputDeviceInfo> &devices)
{
    QList<RowOption> options;
    options.reserve(devices.size());
    for (const AudioInputDeviceInfo &device : devices) {
        options.append({device.id,
                        device.isDefault ? QStringLiteral("%1 (default)").arg(device.label)
                                         : device.label});
    }
    return options;
}

void populateAudioInputDevices(QComboBox *combo,
                               const QList<AudioInputDeviceInfo> &devices,
                               const QString &selectedDeviceId)
{
    const QSignalBlocker blocker(combo);
    combo->clear();
    for (const RowOption &option : audioDeviceOptions(audioInputDeviceOptions(devices),
                                                      selectedDeviceId)) {
        combo->addItem(option.label, option.id);
        if (!option.enabled) {
            setComboItemEnabled(combo, combo->count() - 1, false, option.help);
        }
    }
    selectData(combo, selectedDeviceId);
}

QColor positiveTextColor(const QPalette &palette)
{
#ifdef SPEECHER_WITH_KCOLORSCHEME
    const KColorScheme colors(palette.currentColorGroup(), KColorScheme::View);
    return colors.foreground(KColorScheme::PositiveText).color();
#else
    // Without a scheme that names a positive colour, plain text: link blue
    // would read as something to click.
    return palette.color(QPalette::WindowText);
#endif
}

QLabel *makePageTitle(const QString &text, QWidget *parent)
{
    auto *title = new QLabel(text, parent);
    title->setObjectName(QStringLiteral("pageTitle"));
    title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont font = QApplication::font();
    if (font.pointSizeF() > 0) {
        font.setPointSizeF(font.pointSizeF() * 1.4);
    } else if (font.pixelSize() > 0) {
        font.setPixelSize(qRound(font.pixelSize() * 1.4));
    }
    font.setBold(false);
    title->setFont(font);
    return title;
}

QVBoxLayout *makeSettingsPage(QScrollArea *scroll)
{
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setBackgroundRole(QPalette::Window);
    scroll->viewport()->setBackgroundRole(QPalette::Window);

    auto *page = new QWidget(scroll);
    page->setAutoFillBackground(false);
    auto *layout = new QVBoxLayout(page);
    // The same breathing room around the cards as the rows keep inside them.
    layout->setContentsMargins(rowHorizontalPadding(),
                               rowVerticalPadding(),
                               rowHorizontalPadding(),
                               rowVerticalPadding());
    scroll->setWidget(page);
    return layout;
}

} // namespace speecher::settings
