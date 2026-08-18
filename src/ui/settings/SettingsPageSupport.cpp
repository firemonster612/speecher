#include "ui/settings/SettingsPageSupport.h"

#include <QApplication>
#include <QCheckBox>
#include "dictation/DictationPorts.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QScrollArea>
#include <QRegion>
#include <QResizeEvent>
#include <QFile>
#include <QTextStream>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QStandardItemModel>
#include <QStandardPaths>
#include <QStyle>
#include <QVBoxLayout>

#ifdef SPEECHER_WITH_KCOLORSCHEME
#include <KColorScheme>
#endif

namespace speecher::settings {

namespace {

constexpr qreal cardRadius = 16.0;

QColor blendedColor(const QColor &base, const QColor &foreground, int foregroundPercent)
{
    const int basePercent = 100 - foregroundPercent;
    return QColor((base.red() * basePercent + foreground.red() * foregroundPercent) / 100,
                  (base.green() * basePercent + foreground.green() * foregroundPercent) / 100,
                  (base.blue() * basePercent + foreground.blue() * foregroundPercent) / 100);
}

QColor cardColor(const QPalette &palette)
{
    const QColor window = palette.color(QPalette::Window);
    const QColor text = palette.color(QPalette::WindowText);
    return window.lightness() < text.lightness()
        ? blendedColor(window, text, 7)
        : palette.color(QPalette::Base);
}

class SettingsCard final : public QFrame {
public:
    explicit SettingsCard(QWidget *parent)
        : QFrame(parent)
    {
        setAttribute(Qt::WA_OpaquePaintEvent, false);
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QFrame::resizeEvent(event);
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), cardRadius, cardRadius);
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor background = cardColor(palette());
        QColor border = blendedColor(background, palette().color(QPalette::Text), 12);
        if (property("accentCard").toBool()) {
            const QColor accent = palette().color(QPalette::Highlight);
            background = blendedColor(background, accent, 12);
            border = blendedColor(background, accent, 38);
        }
        painter.setBrush(background);
        painter.setPen(QPen(border, 1.0));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                cardRadius,
                                cardRadius);
    }
};

class SettingsSeparator final : public QFrame {
public:
    explicit SettingsSeparator(QWidget *parent)
        : QFrame(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), separatorColor(palette()));
    }
};

} // namespace

QColor separatorColor(const QPalette &palette)
{
    const QColor background = cardColor(palette);
    const QColor window = palette.color(QPalette::Window);
    return window.lightness() < palette.color(QPalette::WindowText).lightness()
        ? blendedColor(background, window, 82)
        : blendedColor(background, palette.color(QPalette::Text), 8);
}

QFrame *makeSeparator(QWidget *parent)
{
    auto *line = new SettingsSeparator(parent);
    line->setObjectName(QStringLiteral("settingsSeparator"));
    line->setFrameShape(QFrame::NoFrame);
    line->setFixedHeight(1);
    line->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return line;
}

QWidget *makeCenteredSeparator(QWidget *parent)
{
    auto *container = new QWidget(parent);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addStretch(1);
    layout->addWidget(makeSeparator(container), 3);
    layout->addStretch(1);
    return container;
}

void configureFormLayout(QFormLayout *form)
{
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setFormAlignment(Qt::AlignHCenter | Qt::AlignTop);
    form->setLabelAlignment(Qt::AlignRight);
}

QFrame *makeRow(const QString &label,
                const QString &description,
                QWidget *control,
                QWidget *parent,
                QWidget *titleAccessory)
{
    auto *row = new QFrame(parent);
    row->setObjectName(QStringLiteral("settingsRow"));
    row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    auto *rowLayout = new QHBoxLayout(row);
    rowLayout->setContentsMargins(14, 10, 14, 10);
    rowLayout->setSpacing(16);

    auto *text = new QWidget(row);
    text->setObjectName(QStringLiteral("rowLabelCell"));
    text->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *textLayout = new QVBoxLayout(text);
    textLayout->setContentsMargins(0, 0, 0, 0);
    textLayout->setSpacing(2);

    auto *title = new QLabel(label, text);
    title->setObjectName(QStringLiteral("rowTitle"));
    title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (titleAccessory) {
        auto *titleRow = new QWidget(text);
        titleRow->setObjectName(QStringLiteral("rowText"));
        auto *titleLayout = new QHBoxLayout(titleRow);
        titleLayout->setContentsMargins(0, 0, 0, 0);
        titleLayout->addWidget(title, 0, Qt::AlignVCenter);
        titleLayout->addWidget(titleAccessory, 0, Qt::AlignVCenter);
        titleLayout->addStretch();
        textLayout->addWidget(titleRow);
    } else {
        textLayout->addWidget(title);
    }

    if (!description.isEmpty()) {
        auto *subtitle = new QLabel(description, text);
        subtitle->setObjectName(QStringLiteral("rowDescription"));
        subtitle->setWordWrap(true);
        subtitle->setForegroundRole(QPalette::PlaceholderText);
        textLayout->addWidget(subtitle);
    }

    if (auto *checkBox = qobject_cast<QCheckBox *>(control)) {
        checkBox->setText({});
        if (checkBox->accessibleName().isEmpty()) {
            checkBox->setAccessibleName(label);
        }
    }

    rowLayout->addWidget(text, 1);
    rowLayout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
    return row;
}

void addRow(QFormLayout *layout, QFrame *row, QWidget *parent, bool addSeparator)
{
    layout->addRow(row);
    if (addSeparator) {
        layout->addRow(makeSeparator(parent));
    }
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

void applyLabelHierarchy(QWidget *root)
{
    for (QLabel *label : root->findChildren<QLabel *>()) {
        if (label->objectName() == QStringLiteral("subsectionLabel")) {
            QFont font = label->font();
            font.setBold(true);
            label->setFont(font);
        } else if (label->objectName() == QStringLiteral("rowDescription")
                   || label->objectName() == QStringLiteral("noteText")) {
            label->setForegroundRole(QPalette::PlaceholderText);
        }
    }
}

void populateAudioInputDevices(QComboBox *combo,
                               const QList<AudioInputDeviceInfo> &devices,
                               const QString &selectedDeviceId)
{
    const QSignalBlocker blocker(combo);
    combo->clear();

    if (devices.isEmpty()) {
        combo->addItem(QStringLiteral("No microphones found"), QString());
        setComboItemEnabled(combo,
                            0,
                            false,
                            QStringLiteral("Connect or enable an input device, then try again."));
        if (!selectedDeviceId.isEmpty()) {
            combo->addItem(QStringLiteral("Missing microphone"), selectedDeviceId);
            setComboItemEnabled(combo,
                                1,
                                false,
                                QStringLiteral("This saved microphone is not currently available."));
            selectData(combo, selectedDeviceId);
        }
        return;
    }

    combo->addItem(QStringLiteral("System default"), QString());
    bool selectedFound = selectedDeviceId.isEmpty();
    for (const AudioInputDeviceInfo &device : devices) {
        combo->addItem(device.isDefault
                           ? QStringLiteral("%1 (default)").arg(device.label)
                           : device.label,
                       device.id);
        selectedFound = selectedFound || device.id == selectedDeviceId;
    }

    if (!selectedFound) {
        combo->addItem(QStringLiteral("Missing microphone"), selectedDeviceId);
        setComboItemEnabled(combo,
                            combo->count() - 1,
                            false,
                            QStringLiteral("This saved microphone is not currently available."));
    }
    selectData(combo, selectedDeviceId);
}

QColor positiveTextColor(const QPalette &palette)
{
#ifdef SPEECHER_WITH_KCOLORSCHEME
    const KColorScheme colors(palette.currentColorGroup(), KColorScheme::View);
    return colors.foreground(KColorScheme::PositiveText).color();
#else
    return palette.color(QPalette::Link);
#endif
}

QLabel *makeSectionLabel(const QString &text, QWidget *parent)
{
    auto *section = new QLabel(text, parent);
    section->setObjectName(QStringLiteral("sectionLabel"));
    section->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    QFont font = section->font();
    font.setBold(true);
    section->setFont(font);
    return section;
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

QFrame *makeSettingsCard(QWidget *parent)
{
    auto *card = new SettingsCard(parent);
    card->setObjectName(QStringLiteral("settingsCard"));
    card->setFrameShape(QFrame::NoFrame);
    card->setBackgroundRole(QPalette::Base);
    card->setAutoFillBackground(false);
    auto *layout = new QFormLayout(card);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setHorizontalSpacing(0);
    layout->setVerticalSpacing(0);
    configureFormLayout(layout);
    return card;
}

QVBoxLayout *makeSettingsPage(QScrollArea *scroll)
{
    scroll->setObjectName(QStringLiteral("settingsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setBackgroundRole(QPalette::Window);
    scroll->viewport()->setBackgroundRole(QPalette::Window);

    auto *page = new QWidget(scroll);
    page->setObjectName(QStringLiteral("settingsRiver"));
    page->setMaximumWidth(560);
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    page->setAutoFillBackground(false);
    auto *layout = new QVBoxLayout(page);
    applyPageMargins(layout);
    scroll->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    scroll->setWidget(page);
    return layout;
}

} // namespace speecher::settings
