#include "ui/HomePage.h"

#include "app/ApplicationController.h"
#include "core/InsightsExport.h"
#include "core/InsightsLog.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "ui/AccessibilityNotice.h"
#include "ui/WaveformWidget.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QProgressBar>
#include <QSaveFile>
#include <QStandardPaths>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace speecher {

namespace {

// Below this column width the two-up cards stack. The column is capped like
// the settings cards, so a 1040 px window keeps both rows side by side and a
// 720 px one stacks them. The tiles go by the width of their own lines.
constexpr int kTwoUpMinimumWidth = 560;
// The lighter tint for every progress bar but the leading one. Breeze draws a
// progress fill darker than its palette colour, so the charts' 42 % mix would
// sink into the groove; this reads at the same step below the lead bar.
constexpr int kMutedProgressPercent = 70;

QString number(qint64 value)
{
    return QLocale().toString(value);
}

QIcon themedIcon(const QString &name, const QString &fallback = QString())
{
    return QIcon::fromTheme(name, QIcon::fromTheme(fallback));
}

// A stat tile's icon (insightTileIconId). Breeze has no flame, so the streak
// falls back to its Hotspot flame, then to the trophy.
QIcon tileIcon(const QString &iconId)
{
    static const QHash<QString, QStringList> names{
        {QStringLiteral("text"), {QStringLiteral("format-justify-left")}},
        {QStringLiteral("flame"),
         {QStringLiteral("flame"), QStringLiteral("hotspot-symbolic"), QStringLiteral("games-highscores")}},
        {QStringLiteral("microphone"), {QStringLiteral("audio-input-microphone")}},
        {QStringLiteral("waveform"), {QStringLiteral("waveform")}},
    };
    for (const QString &name : names.value(iconId)) {
        if (QIcon::hasThemeIcon(name)) return QIcon::fromTheme(name);
    }
    return {};
}

QLabel *mutedLabel(const QString &text, QWidget *parent, bool small = true)
{
    auto *label = new QLabel(text, parent);
    label->setForegroundRole(QPalette::PlaceholderText);
    label->setWordWrap(true);
    if (small) label->setFont(settings::smallFont(label->font()));
    return label;
}

QLabel *boldLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    return label;
}

// A settings card whose content is a free column instead of form rows.
QFrame *makeCard(QWidget *parent, QVBoxLayout **content)
{
    // The card's form sizes rows from their size hints, which overstates the
    // height of wrapped text, so free content replaces the form outright.
    QFrame *card = settings::makeSettingsCard(parent);
    delete settings::cardFormLayout(card)->parentWidget();
    auto *layout = qobject_cast<QVBoxLayout *>(card->layout());
    const QMargins padding = settings::rowPadding();
    layout->setContentsMargins(padding.left(), settings::largeSpacing(), padding.right(),
                               settings::largeSpacing());
    layout->setSpacing(settings::relatedSpacing());
    *content = layout;
    return card;
}

QFrame *makeTitledCard(const QString &title, QWidget *parent, QVBoxLayout **content)
{
    QFrame *card = makeCard(parent, content);
    (*content)->addWidget(boldLabel(title, card));
    return card;
}

// A big light number with its unit in small grey text on the same baseline.
QWidget *bigNumber(const QList<QPair<QString, QString>> &parts, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(settings::tightSpacing());
    for (const auto &[value, unit] : parts) {
        auto *valueLabel = new QLabel(value, row);
        valueLabel->setObjectName(QStringLiteral("insightValue"));
        QFont font = valueLabel->font();
        font.setPointSizeF(font.pointSizeF() * 2);
        font.setWeight(QFont::Light);
        valueLabel->setFont(font);
        layout->addWidget(valueLabel, 0, Qt::AlignBottom);
        if (unit.isEmpty()) continue;
        QLabel *unitLabel = mutedLabel(unit, row, false);
        unitLabel->setWordWrap(false);
        // Bottom-aligned labels of two sizes share a baseline once the smaller
        // one is lifted by the difference in descent.
        unitLabel->setContentsMargins(
            0, 0, settings::tightSpacing(),
            QFontMetrics(font).descent() - unitLabel->fontMetrics().descent());
        layout->addWidget(unitLabel, 0, Qt::AlignBottom);
    }
    layout->addStretch();
    return row;
}

// A bar in the accent colour, or the lighter tint for every bar but the lead.
QProgressBar *makeBar(qint64 value, qint64 maximum, bool leading, QWidget *parent)
{
    auto *bar = new QProgressBar(parent);
    // A progress bar counts in int; past that, the same ratio in fewer steps.
    const double step = std::max(1.0, double(maximum) / std::numeric_limits<int>::max());
    bar->setRange(0, std::max(1, int(maximum / step)));
    bar->setValue(int(value / step));
    bar->setTextVisible(false);
    bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (!leading) {
        // Qt 6 styles (Breeze among them) fill progress from Accent, older
        // ones from Highlight; the tint goes in both.
        const QColor tint = accentTint(bar->palette(), kMutedProgressPercent);
        QPalette tinted = bar->palette();
        tinted.setColor(QPalette::Highlight, tint);
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
        tinted.setColor(QPalette::Accent, tint);
#endif
        bar->setPalette(tinted);
    }
    return bar;
}

// One "name, bar, value" line of the pace and apps cards.
void addBarRow(QGridLayout *grid,
               QWidget *name,
               QProgressBar *bar,
               const QString &value,
               const QString &toolTip = QString())
{
    const int row = grid->rowCount();
    auto *valueLabel = new QLabel(value, grid->parentWidget());
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    grid->addWidget(name, row, 0);
    grid->addWidget(bar, row, 1);
    grid->addWidget(valueLabel, row, 2);
    for (QWidget *widget : {name, static_cast<QWidget *>(bar), static_cast<QWidget *>(valueLabel)}) {
        widget->setToolTip(toolTip);
    }
}

QGridLayout *makeBarGrid(QVBoxLayout *content)
{
    auto *host = new QWidget(content->parentWidget());
    auto *grid = new QGridLayout(host);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(settings::largeSpacing());
    grid->setVerticalSpacing(settings::relatedSpacing());
    grid->setColumnStretch(1, 1);
    content->addWidget(host);
    return grid;
}

// Clamps text to two lines at the label's width, ending in an ellipsis.
QString twoLines(const QString &text, const QFont &font, int width)
{
    QTextLayout layout(text, font);
    layout.beginLayout();
    QTextLine first = layout.createLine();
    if (!first.isValid()) {
        layout.endLayout();
        return text;
    }
    first.setLineWidth(width);
    QTextLine second = layout.createLine();
    if (!second.isValid()) {
        layout.endLayout();
        return text;
    }
    second.setLineWidth(width);
    const bool more = layout.createLine().isValid();
    layout.endLayout();
    if (!more) return text;
    const QString rest = text.mid(second.textStart());
    return text.left(second.textStart())
        + QFontMetrics(font).elidedText(rest.simplified(), Qt::ElideRight, width);
}


// "12 days with dictation in the last year", or in the weeks the heatmap
// has room for, then Less, the levels and More.
void addHeatLegend(QVBoxLayout *content, const InsightsSummary &summary, InsightsHeatmap *heatmap,
                   QWidget *host)
{
    auto *foot = new QHBoxLayout;
    foot->setSpacing(settings::relatedSpacing());
    QLabel *span = mutedLabel(activeDaysLastYearText(summary.activeDaysLastYear), host);
    QObject::connect(heatmap, &InsightsHeatmap::drawnWeeksChanged, span,
                     [span, summary](int weeks) { span->setText(heatmapSpanText(summary, weeks)); });
    foot->addWidget(span, 1);
    foot->addWidget(mutedLabel(heatLegendLessText(), host));
    foot->addWidget(new InsightsHeatmap(InsightsHeatmap::Shape::Legend, host));
    foot->addWidget(mutedLabel(heatLegendMoreText(), host));
    content->addLayout(foot);
}

// The picture "Copy image with stats" puts on the clipboard: the period's four
// numbers over the year's heatmap, drawn by the same widgets as Home at twice
// the scale so it stays sharp wherever it is pasted.
QImage statsImage(const InsightsSummary &summary, InsightsRange range, HeatMeasure measure)
{
    QWidget root;
    root.setAttribute(Qt::WA_DontShowOnScreen);
    root.setAutoFillBackground(true);
    auto *outer = new QVBoxLayout(&root);
    outer->setContentsMargins(QMargins() + settings::largeSpacing());
    QVBoxLayout *content = nullptr;
    QFrame *card = makeCard(&root, &content);
    outer->addWidget(card);
    QWidget *host = content->parentWidget();

    auto *title = new QHBoxLayout;
    title->addWidget(boldLabel(insightsImageTitle(), host), 1);
    QLabel *period = mutedLabel(insightsRangeLabel(range), host, false);
    period->setWordWrap(false);
    title->addWidget(period);
    content->addLayout(title);

    auto *numbers = new QHBoxLayout;
    numbers->setSpacing(settings::largeSpacing() * 2);
    // The tiles' figures, in their order and under their titles.
    for (const InsightTileText &tile : insightTiles(summary, QDate())) {
        auto *column = new QVBoxLayout;
        column->setSpacing(0);
        QLabel *caption = mutedLabel(tile.title, host, false);
        caption->setWordWrap(false);
        column->addWidget(caption);
        column->addWidget(bigNumber({{tile.value, tile.unit}}, host));
        numbers->addLayout(column);
    }
    numbers->addStretch();
    content->addLayout(numbers);
    if (const QString pace = insightsImagePaceLine(summary); !pace.isEmpty()) {
        content->addWidget(mutedLabel(pace, host, false));
    }

    content->addSpacing(settings::relatedSpacing());
    auto *heatmap = new InsightsHeatmap(InsightsHeatmap::Shape::Year, host);
    heatmap->setDays(summary.heatmap);
    heatmap->setMeasure(measure);
    heatmap->setFixedWidth(heatmap->sizeHint().width());
    content->addWidget(heatmap);
    addHeatLegend(content, summary, heatmap, host);

    root.adjustSize();
    constexpr qreal scale = 2;
    QImage image(root.size() * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(root.palette().color(QPalette::Window));
    root.render(&image);
    return image;
}

} // namespace

HomePage::HomePage(ApplicationController *controller, QWidget *parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_scroll(new QScrollArea(this))
    , m_column(new QWidget)
    , m_accessibilityNotice(new AccessibilityNotice(m_column))
{
    setObjectName(QStringLiteral("homePage"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_scroll);

    m_columnLayout = new QVBoxLayout(m_column);
    settings::applyPageMargins(m_columnLayout);
    m_columnLayout->setSpacing(settings::relatedSpacing());
    settings::configurePageScroll(m_scroll, m_column);
    m_column->installEventFilter(this);

    m_accessibilityNotice->setCompact(true);
    m_columnLayout->addWidget(m_accessibilityNotice);
    m_columnLayout->addWidget(buildDictationCard(m_column));

    QVBoxLayout *noticeContent = nullptr;
    m_notice = makeCard(m_column, &noticeContent);
    {
        m_notice->setObjectName(QStringLiteral("insightsNotice"));
        QWidget *host = m_notice;
        auto *row = new QHBoxLayout;
        row->setSpacing(settings::largeSpacing());
        auto *text = new QVBoxLayout;
        text->setSpacing(0);
        auto *title = boldLabel(QString(), host);
        title->setObjectName(QStringLiteral("insightsNoticeTitle"));
        auto *body = mutedLabel(QString(), host, false);
        body->setObjectName(QStringLiteral("insightsNoticeBody"));
        text->addWidget(title);
        text->addWidget(body);
        row->addLayout(text, 1);
        m_noticeButton = new QPushButton(homeText(HomeText::InsightsSettings), host);
        m_noticeButton->setObjectName(QStringLiteral("insightsNoticeSettings"));
        row->addWidget(m_noticeButton, 0, Qt::AlignVCenter);
        noticeContent->addLayout(row);
        connect(m_noticeButton, &QPushButton::clicked, this,
                [this] { emit pageRequested(QStringLiteral("general")); });
    }
    m_columnLayout->addWidget(m_notice);

    m_insightsHeader = new QWidget(m_column);
    {
        auto *header = new QHBoxLayout(m_insightsHeader);
        // The controls end where the cards' content does.
        header->setContentsMargins(0, settings::relatedSpacing(), settings::rowPadding().right(), 0);
        header->addWidget(settings::makeSectionLabel(homeText(HomeText::YourDictation), m_insightsHeader),
                          1, Qt::AlignBottom);
        m_range = new QComboBox(m_insightsHeader);
        m_range->setObjectName(QStringLiteral("insightsRange"));
        m_range->setAccessibleName(homeText(HomeText::Period));
        for (const InsightsRange range : {InsightsRange::Last7Days, InsightsRange::Last30Days,
                                          InsightsRange::ThisYear, InsightsRange::AllTime}) {
            m_range->addItem(insightsRangeLabel(range), int(range));
        }
        m_range->setCurrentIndex(1);
        header->addWidget(m_range);
        connect(m_range, &QComboBox::currentIndexChanged, this, &HomePage::refresh);
        header->addWidget(buildShareButton(m_insightsHeader));
    }
    m_columnLayout->addWidget(m_insightsHeader);
    m_columnLayout->addStretch();

    connect(controller->insightsLog(), &InsightsLog::changed, this, &HomePage::refresh);
    connect(m_toggle, &QPushButton::clicked, controller, &ApplicationController::toggle);
    connect(controller, &ApplicationController::stateChanged, this, &HomePage::applyState);
    connect(controller, &ApplicationController::statusChanged, m_status, &QLabel::setText);
    connect(controller, &ApplicationController::audioLevelChanged, m_waveform, &WaveformWidget::setLevel);
    connect(controller, &ApplicationController::transcriptDelivered, this,
            &HomePage::refreshLastTranscript);
    connect(controller, &ApplicationController::lastRecordChanged, this,
            &HomePage::refreshLastTranscript);
    connect(controller, &ApplicationController::globalShortcutChanged, this,
            &HomePage::updateShortcutHint);
    connect(controller, &ApplicationController::globalShortcutSupportChanged, this,
            &HomePage::updateShortcutHint);
    connect(controller, &ApplicationController::globalShortcutRegistrationFinished, this,
            &HomePage::updateShortcutHint);
    connect(m_accessibilityNotice, &AccessibilityNotice::enableRequested, this, [this] {
        QString error;
        if (!m_controller->enableAccessibility(&error)) {
            m_accessibilityNotice->showError(error);
        }
    });
    connect(controller, &ApplicationController::accessibilityStateChanged, this,
            [this](bool supported, bool enabled, bool persistent) {
                if (!supported) {
                    m_accessibilityNotice->hide();
                } else {
                    m_accessibilityNotice->setState(supported, enabled, persistent);
                }
                // The learned corrections card says whether accessibility
                // holds learning back.
                refresh();
            });
    if (controller->accessibilitySupported()) {
        m_accessibilityNotice->setState(true,
                                        controller->accessibilityEnabled(),
                                        controller->accessibilityPersistent());
    } else {
        m_accessibilityNotice->hide();
    }
    setStatus(controller->stateName());
    updateShortcutHint();
    refresh();
}

QFrame *HomePage::buildDictationCard(QWidget *parent)
{
    QVBoxLayout *content = nullptr;
    QFrame *card = makeCard(parent, &content);
    card->setObjectName(QStringLiteral("dictationCard"));
    QWidget *host = content->parentWidget();

    auto *top = new QHBoxLayout;
    top->setSpacing(settings::largeSpacing());
    auto *text = new QVBoxLayout;
    text->setSpacing(0);
    auto *statusRow = new QHBoxLayout;
    statusRow->setSpacing(settings::relatedSpacing());
    m_status = boldLabel(QString(), host);
    m_status->setObjectName(QStringLiteral("dictationStatus"));
    m_waveform = new WaveformWidget(host);
    m_waveform->setCompact(true);
    statusRow->addWidget(m_status);
    statusRow->addWidget(m_waveform, 0, Qt::AlignVCenter);
    statusRow->addStretch();
    text->addLayout(statusRow);
    m_hint = mutedLabel(QString(), host, false);
    m_hint->setObjectName(QStringLiteral("dictationHint"));
    m_hint->setTextFormat(Qt::RichText);
    connect(m_hint, &QLabel::linkActivated, this, &HomePage::pageRequested);
    text->addWidget(m_hint);
    // The popup shows a failure for a few seconds and cannot take focus, so the
    // reason also stays here until the next session starts (lastFailure).
    m_errorText = new QLabel(host);
    m_errorText->setObjectName(QStringLiteral("dictationError"));
    m_errorText->setWordWrap(true);
    m_errorText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_errorText->hide();
    text->addWidget(m_errorText);
    top->addLayout(text, 1);
    m_toggle = new QPushButton(host);
    m_toggle->setObjectName(QStringLiteral("dictationToggle"));
    top->addWidget(m_toggle, 0, Qt::AlignVCenter);
    content->addLayout(top);

    m_lastRow = new QWidget(host);
    auto *lastLayout = new QVBoxLayout(m_lastRow);
    lastLayout->setContentsMargins(0, settings::tightSpacing(), 0, 0);
    lastLayout->setSpacing(settings::relatedSpacing());
    lastLayout->addWidget(settings::makeSeparator(m_lastRow));
    auto *last = new QHBoxLayout;
    last->setSpacing(settings::largeSpacing());
    m_lastText = new QLabel(m_lastRow);
    m_lastText->setObjectName(QStringLiteral("lastTranscript"));
    m_lastText->setWordWrap(true);
    m_lastText->setTextFormat(Qt::PlainText);
    m_lastText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_lastText->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_lastText->installEventFilter(this);
    last->addWidget(m_lastText, 1, Qt::AlignTop);
    m_lastMeta = mutedLabel(QString(), m_lastRow);
    m_lastMeta->setObjectName(QStringLiteral("lastTranscriptMeta"));
    m_lastMeta->setWordWrap(false);
    last->addWidget(m_lastMeta, 0, Qt::AlignTop);

    m_copyTranscript = new QToolButton(m_lastRow);
    m_copyTranscript->setObjectName(QStringLiteral("copyTranscript"));
    const QIcon copyIcon = themedIcon(QStringLiteral("edit-copy"));
    // Without a themed icon an icon-only button is invisible; fall back to a word.
    m_copyTranscript->setText(QStringLiteral("Copy"));
    m_copyTranscript->setIcon(copyIcon);
    m_copyTranscript->setToolButtonStyle(copyIcon.isNull() ? Qt::ToolButtonTextOnly
                                                           : Qt::ToolButtonIconOnly);
    m_copyTranscript->setAutoRaise(true);
    m_copyTranscript->setToolTip(copyTranscriptCaption());
    last->addWidget(m_copyTranscript, 0, Qt::AlignTop);
    connect(m_copyTranscript, &QToolButton::clicked, this, [this, copyIcon] {
        const QString transcript = m_controller->session()->lastTranscript();
        if (transcript.isEmpty()) return;
        QGuiApplication::clipboard()->setText(transcript);
        m_copyTranscript->setIcon(themedIcon(QStringLiteral("checkmark"),
                                             QStringLiteral("dialog-ok-apply")));
        m_copyTranscript->setText(copiedCaption());
        QTimer::singleShot(kCopiedFeedbackMs, m_copyTranscript, [this, copyIcon] {
            m_copyTranscript->setIcon(copyIcon);
            m_copyTranscript->setText(QStringLiteral("Copy"));
        });
    });
    lastLayout->addLayout(last);
    content->addWidget(m_lastRow);
    refreshLastTranscript();
    return card;
}

void HomePage::refresh()
{
    const int scroll = m_scroll->verticalScrollBar()->value();
    delete m_insights;
    m_insights = nullptr;
    m_tileGrid = nullptr;
    m_tiles.clear();
    m_tileMinimumWidth = 0;
    m_pairs.clear();

    const bool enabled = m_controller->settings()->insightsEnabled();
    const QList<DictationRecord> &records = m_controller->insightsLog()->records();
    auto *title = m_notice->findChild<QLabel *>(QStringLiteral("insightsNoticeTitle"));
    auto *body = m_notice->findChild<QLabel *>(QStringLiteral("insightsNoticeBody"));
    if (!enabled) {
        title->setText(homeText(HomeText::InsightsOffTitle));
        body->setText(homeText(HomeText::InsightsOffBody));
    } else {
        title->setText(homeText(HomeText::NoInsightsTitle));
        body->setText(homeText(HomeText::NoInsightsBody));
    }
    m_noticeButton->setVisible(!enabled);
    const bool showStats = enabled && !records.isEmpty();
    m_notice->setVisible(!showStats);
    m_insightsHeader->setVisible(showStats);
    m_summarizedDay = m_controller->insightsToday();
    if (showStats) {
        m_insights = buildInsights(summarize(records, currentRange(), m_summarizedDay,
                                               m_controller->settings()->writingProfileSettings()));
        m_columnLayout->insertWidget(m_columnLayout->indexOf(m_insightsHeader) + 1, m_insights);
        applyWidth();
    }
    refreshLastTranscript();
    QTimer::singleShot(0, m_scroll, [this, scroll] { m_scroll->verticalScrollBar()->setValue(scroll); });
}

QWidget *HomePage::buildInsights(const InsightsSummary &summary)
{
    auto *insights = new QWidget(m_column);
    insights->setObjectName(QStringLiteral("insights"));
    auto *layout = new QVBoxLayout(insights);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(settings::relatedSpacing());
    layout->addWidget(buildTiles(summary, insights));
    layout->addWidget(buildActivityCard(summary, insights));
    const auto pair = [this, insights, layout](QFrame *left, QFrame *right) {
        auto *row = new QBoxLayout(QBoxLayout::LeftToRight);
        row->setSpacing(settings::relatedSpacing());
        row->addWidget(left, 1);
        row->addWidget(right, 1);
        layout->addLayout(row);
        m_pairs.append(row);
    };
    pair(buildHoursCard(summary, insights), buildPaceCard(summary, insights));
    pair(buildAppsCard(summary, insights), buildCorrectionsCard(insights));
    layout->addSpacing(settings::relatedSpacing());
    layout->addWidget(settings::makeSectionLabel(homeText(HomeText::Records), insights));
    layout->addWidget(buildRecordsCard(summary, insights));
    layout->addWidget(buildFooter(insights));
    return insights;
}

QWidget *HomePage::buildTiles(const InsightsSummary &summary, QWidget *parent)
{
    auto *host = new QWidget(parent);
    host->setObjectName(QStringLiteral("insightTiles"));
    m_tileGrid = new QGridLayout(host);
    m_tileGrid->setContentsMargins(0, 0, 0, 0);
    m_tileGrid->setSpacing(settings::relatedSpacing());

    for (const InsightTileText &text : insightTiles(summary, m_controller->insightsToday())) {
        QVBoxLayout *content = nullptr;
        QFrame *card = makeCard(host, &content);
        card->setObjectName(QStringLiteral("insightTile"));
        content->setSpacing(0);
        QWidget *cardHost = content->parentWidget();
        auto *title = new QHBoxLayout;
        title->setSpacing(settings::tightSpacing());
        const QIcon icon = tileIcon(text.iconId);
        if (!icon.isNull()) {
            auto *iconLabel = new QLabel(cardHost);
            const int extent = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
            iconLabel->setPixmap(icon.pixmap(extent, extent));
            title->addWidget(iconLabel);
        }
        QLabel *titleLabel = mutedLabel(text.title, cardHost, false);
        titleLabel->setWordWrap(false);
        title->addWidget(titleLabel, 1);
        content->addLayout(title);
        content->addWidget(bigNumber({{text.value, text.unit}}, cardHost));
        QList<QLabel *> lines;
        for (int index = 0; index < text.lines.size(); ++index) {
            QLabel *line = mutedLabel(text.lines.at(index), cardHost);
            if (index == 0) line->setToolTip(text.firstLineTip);
            content->addWidget(line);
            lines.append(line);
        }
        if (text.showsWeek) {
            auto *week = new InsightsHeatmap(InsightsHeatmap::Shape::Week, cardHost);
            week->setObjectName(QStringLiteral("streakWeek"));
            week->setDays(summary.heatmap);
            week->setAccessibleName(text.title);
            week->setAccessibleDescription(weekDescription(summary));
            content->addSpacing(settings::tightSpacing());
            content->addWidget(week);
        }
        content->addStretch();
        // Four across only when every row fits with no line wrapped: the
        // card's minimum width while its lines cannot wrap.
        for (QLabel *line : std::as_const(lines)) line->setWordWrap(false);
        content->invalidate();
        m_tileMinimumWidth = std::max(m_tileMinimumWidth, card->minimumSizeHint().width());
        for (QLabel *line : std::as_const(lines)) line->setWordWrap(true);
        m_tiles.append(card);
    }
    return host;
}

QFrame *HomePage::buildActivityCard(const InsightsSummary &summary, QWidget *parent)
{
    QVBoxLayout *content = nullptr;
    QFrame *card = makeCard(parent, &content);
    card->setObjectName(QStringLiteral("activityCard"));
    QWidget *host = content->parentWidget();
    auto *head = new QHBoxLayout;
    head->addWidget(boldLabel(homeText(HomeText::Activity), host), 1);
    auto *measure = new QComboBox(host);
    measure->setObjectName(QStringLiteral("activityMeasure"));
    measure->setAccessibleName(homeText(HomeText::Measure));
    for (const HeatMeasure value : {HeatMeasure::Dictations, HeatMeasure::Words, HeatMeasure::Audio}) {
        measure->addItem(heatMeasureLabel(value), int(value));
    }
    measure->setCurrentIndex(measure->findData(int(m_measure)));
    head->addWidget(measure);
    content->addLayout(head);

    auto *heatmap = new InsightsHeatmap(InsightsHeatmap::Shape::Year, host);
    heatmap->setObjectName(QStringLiteral("activityHeatmap"));
    heatmap->setDays(summary.heatmap);
    heatmap->setMeasure(m_measure);
    heatmap->setAccessibleName(homeText(HomeText::Activity));
    heatmap->setAccessibleDescription(heatmapDescription(summary, m_measure));
    content->addWidget(heatmap);
    connect(measure, &QComboBox::currentIndexChanged, heatmap, [this, measure, heatmap, summary] {
        m_measure = static_cast<HeatMeasure>(measure->currentData().toInt());
        heatmap->setMeasure(m_measure);
        heatmap->setAccessibleDescription(heatmapDescription(summary, m_measure));
    });

    addHeatLegend(content, summary, heatmap, host);
    return card;
}

QFrame *HomePage::buildHoursCard(const InsightsSummary &summary, QWidget *parent)
{
    QVBoxLayout *content = nullptr;
    QFrame *card = makeTitledCard(homeText(HomeText::WhenYouTalk), parent, &content);
    card->setObjectName(QStringLiteral("hoursCard"));
    QWidget *host = content->parentWidget();
    if (!summary.hasHourData) {
        content->addWidget(mutedLabel(homeText(HomeText::NoHourData), host, false));
        content->addStretch();
        return card;
    }
    content->addWidget(boldLabel(personaText(summary), host));
    content->addWidget(mutedLabel(peakText(summary), host, false));
    content->addStretch();
    auto *chart = new InsightsBarChart(host);
    chart->setObjectName(QStringLiteral("hoursChart"));
    chart->setCounts(summary.hourCounts, summary.peakHour);
    chart->setAccessibleName(homeText(HomeText::WhenYouTalk));
    chart->setAccessibleDescription(hourChartDescription(summary));
    content->addWidget(chart);
    return card;
}

QFrame *HomePage::buildPaceCard(const InsightsSummary &summary, QWidget *parent)
{
    QVBoxLayout *content = nullptr;
    QFrame *card = makeTitledCard(homeText(HomeText::Pace), parent, &content);
    card->setObjectName(QStringLiteral("paceCard"));
    QWidget *host = content->parentWidget();
    if (summary.dictations == 0) {
        content->addWidget(mutedLabel(homeText(HomeText::NoDictationInPeriod), host, false));
        content->addStretch();
        return card;
    }
    auto *split = new QHBoxLayout;
    split->setSpacing(settings::gridUnit() * 2);
    const auto stat = [host, split](QWidget *value, const QString &caption) {
        auto *column = new QVBoxLayout;
        column->setSpacing(0);
        value->setParent(host);
        column->addWidget(value);
        column->addWidget(mutedLabel(caption, host));
        split->addLayout(column);
    };
    stat(bigNumber({{number(summary.wordsPerMinute), QStringLiteral("wpm")}}, nullptr),
         homeText(HomeText::SpeakingPace));
    const qint64 saved = summary.minutesSavedVersusTyping;
    QList<QPair<QString, QString>> savedParts;
    if (saved >= 60) savedParts.append({number(saved / 60), QStringLiteral("h")});
    if (saved < 60 || saved % 60) savedParts.append({number(saved % 60), QStringLiteral("min")});
    stat(bigNumber(savedParts, nullptr), homeText(HomeText::SavedOverTyping));
    split->addStretch();
    content->addLayout(split);
    content->addStretch();

    QGridLayout *grid = makeBarGrid(content);
    const qint64 scale = std::max<qint64>(summary.wordsPerMinute, 160);
    // The figures are in the big number and the sentence below; the bars
    // only compare them.
    addBarRow(grid, new QLabel(homeText(HomeText::YouSpeaking), host),
              makeBar(summary.wordsPerMinute, scale, true, host), QString());
    addBarRow(grid, new QLabel(homeText(HomeText::TypicalTyping), host),
              makeBar(summary.typingWordsPerMinute, scale, false, host), QString());
    content->addWidget(mutedLabel(summary.speedupText, host));
    return card;
}

QFrame *HomePage::buildAppsCard(const InsightsSummary &summary, QWidget *parent)
{
    QVBoxLayout *content = nullptr;
    QFrame *card = makeTitledCard(homeText(HomeText::WhereYourWordsGo), parent, &content);
    card->setObjectName(QStringLiteral("appsCard"));
    QWidget *host = content->parentWidget();
    if (summary.apps.isEmpty()) {
        content->addWidget(mutedLabel(homeText(HomeText::NoDictationInPeriod), host, false));
        content->addStretch();
        return card;
    }
    QGridLayout *grid = makeBarGrid(content);
    const qint64 most = summary.apps.first().words;
    for (int index = 0; index < summary.apps.size(); ++index) {
        const AppShare &app = summary.apps.at(index);
        auto *name = new QWidget(host);
        auto *nameLayout = new QHBoxLayout(name);
        nameLayout->setContentsMargins(0, 0, 0, 0);
        nameLayout->setSpacing(settings::tightSpacing());
        auto *appLabel = new QLabel(app.name, name);
        appLabel->setTextFormat(Qt::PlainText);
        nameLayout->addWidget(appLabel);
        if (!app.profileLabel.isEmpty()) {
            nameLayout->addWidget(new Badge(app.profileLabel, Badge::Tone::Accent, name), 0, Qt::AlignVCenter);
        }
        nameLayout->addStretch();
        addBarRow(grid, name, makeBar(app.words, most, index == 0, host),
                  QStringLiteral("%1%").arg(app.percent),
                  [&app] {
                      const ChartTip tip = appTip(app);
                      return QStringLiteral("<b>%1</b><br>%2").arg(tip.title.toHtmlEscaped(), tip.detail);
                  }());
    }
    content->addStretch();
    return card;
}

QFrame *HomePage::buildCorrectionsCard(QWidget *parent)
{
    QVBoxLayout *content = nullptr;
    QFrame *card = makeTitledCard(learnedCorrectionsTitle(), parent, &content);
    card->setObjectName(QStringLiteral("correctionsCard"));
    QWidget *host = content->parentWidget();
    const int learned = m_controller->settings()->learnedCorrections().size();
    const bool learning = m_controller->settings()->snapshot().correctionLearningEnabled;
    const bool accessibility = !m_controller->accessibilitySupported() || m_controller->accessibilityEnabled();
    // Nothing learned yet is said by the note alone, not by a big 0.
    if (learned > 0) {
        auto *stat = new QVBoxLayout;
        stat->setSpacing(0);
        stat->addWidget(bigNumber({{number(learned), {}}}, host));
        stat->addWidget(mutedLabel(learnedCorrectionsCaption(learned), host));
        content->addLayout(stat);
    }
    content->addWidget(mutedLabel(learnedCorrectionsNote(learned, learning, accessibility), host, false));
    content->addStretch();
    const QString action = learnedCorrectionsAction(learned, learning);
    if (action.isEmpty()) {
        return card;
    }
    auto *open = new QPushButton(action, host);
    open->setObjectName(QStringLiteral("reviewCorrections"));
    connect(open, &QPushButton::clicked, this,
            [this] { emit pageRequested(QStringLiteral("vocabulary:corrections")); });
    content->addWidget(open, 0, Qt::AlignLeft);
    return card;
}

QFrame *HomePage::buildRecordsCard(const InsightsSummary &summary, QWidget *parent)
{
    QFrame *card = settings::makeSettingsCard(parent);
    card->setObjectName(QStringLiteral("recordsCard"));
    QFormLayout *form = settings::cardFormLayout(card);
    QWidget *host = form->parentWidget();
    for (const InsightRecordText &record : insightRecords(summary, m_controller->insightsToday())) {
        QWidget *value = nullptr;
        if (record.milestoneBar) {
            auto *progress = new QProgressBar(host);
            progress->setObjectName(QStringLiteral("milestoneProgress"));
            progress->setRange(0, summary.nextMilestone);
            progress->setValue(summary.allTimeWords);
            progress->setTextVisible(false);
            progress->setFixedWidth(settings::gridUnit() * 6);
            progress->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            value = progress;
        } else {
            value = new QLabel(record.value, host);
        }
        settings::addCardRow(form, settings::makeRow(record.title, record.detail, value, host), host);
    }
    return card;
}

QPushButton *HomePage::buildShareButton(QWidget *parent)
{
    auto *button = new QPushButton(parent);
    button->setObjectName(QStringLiteral("shareInsights"));
    const InsightsShareLabels labels = insightsShareLabels();
    button->setIcon(themedIcon(QStringLiteral("document-share"), QStringLiteral("emblem-shared")));
    button->setText(labels.share);
    // The button's text says what the last choice did, then goes back.
    const auto report = [button, labels](const QString &text, const QString &tip = QString()) {
        button->setText(text);
        button->setToolTip(tip);
        QTimer::singleShot(tip.isEmpty() ? kCopiedFeedbackMs : 5000, button, [button, labels] {
            button->setText(labels.share);
            button->setToolTip(QString());
        });
    };
    auto *menu = new QMenu(button);
    connect(menu->addAction(themedIcon(QStringLiteral("image-x-generic")), labels.copyImage),
            &QAction::triggered, this, [this, report, labels] {
                copyStatsImage();
                report(labels.copied);
            });
    connect(menu->addAction(themedIcon(QStringLiteral("edit-copy")), labels.copyText),
            &QAction::triggered, this, [this, report, labels] {
                QGuiApplication::clipboard()->setText(insightsShareText(currentSummary(), currentRange()));
                report(labels.copied);
            });
    menu->addSeparator();
    connect(menu->addAction(themedIcon(QStringLiteral("document-save-as")), labels.saveJson),
            &QAction::triggered, this, [this, report, labels] {
                const QString suggested =
                    QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                        .filePath(insightsJsonFileName(m_summarizedDay));
                const QString path = QFileDialog::getSaveFileName(
                    this, labels.saveTitle, suggested, QStringLiteral("%1 (*.json)").arg(labels.jsonFilter));
                if (path.isEmpty()) return;
                QSaveFile file(path);
                if (file.open(QIODevice::WriteOnly)
                    && file.write(insightsJson(currentSummary(), currentRange(), m_summarizedDay)) >= 0
                    && file.commit()) {
                    report(labels.saved);
                } else {
                    report(labels.saveFailed, file.errorString());
                }
            });
    button->setMenu(menu);
    return button;
}

void HomePage::copyStatsImage()
{
    QGuiApplication::clipboard()->setImage(statsImage(currentSummary(), currentRange(), m_measure));
}

InsightsSummary HomePage::currentSummary() const
{
    return summarize(m_controller->insightsLog()->records(), currentRange(), m_summarizedDay,
                     m_controller->settings()->writingProfileSettings());
}

InsightsRange HomePage::currentRange() const
{
    return static_cast<InsightsRange>(m_range->currentData().toInt());
}

QWidget *HomePage::buildFooter(QWidget *parent)
{
    // The lock, then the sentence with its link at the end, in one flow that
    // wraps at the cards' content width.
    auto *footer = new QWidget(parent);
    footer->setObjectName(QStringLiteral("insightsPrivacyNote"));
    auto *line = new QHBoxLayout(footer);
    const QMargins padding = settings::rowPadding();
    line->setContentsMargins(padding.left(), settings::relatedSpacing(), padding.right(), 0);
    line->setSpacing(settings::tightSpacing());
    const QIcon lockIcon = themedIcon(QStringLiteral("object-locked"), QStringLiteral("lock"));
    if (!lockIcon.isNull()) {
        auto *lock = new QLabel(footer);
        const int extent = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
        lock->setPixmap(lockIcon.pixmap(extent, extent));
        line->addWidget(lock, 0, Qt::AlignTop);
    }
    QLabel *note = mutedLabel(QStringLiteral("%1 <a href=\"general\">%2</a>")
                                  .arg(homeText(HomeText::PrivacyNote).toHtmlEscaped(),
                                       homeText(HomeText::InsightsSettings).toHtmlEscaped()),
                              footer);
    note->setTextFormat(Qt::RichText);
    connect(note, &QLabel::linkActivated, this, &HomePage::pageRequested);
    line->addWidget(note, 1);
    return footer;
}

void HomePage::applyWidth()
{
    const int width = m_column->contentsRect().width() - m_columnLayout->contentsMargins().left()
        - m_columnLayout->contentsMargins().right();
    for (QBoxLayout *pair : std::as_const(m_pairs)) {
        pair->setDirection(width < kTwoUpMinimumWidth ? QBoxLayout::TopToBottom
                                                     : QBoxLayout::LeftToRight);
    }
    if (!m_tileGrid) return;
    const int fourAcross = 4 * m_tileMinimumWidth + 3 * m_tileGrid->horizontalSpacing();
    const int columns = width < fourAcross ? 2 : 4;
    if (m_tileGrid->count() == m_tiles.size() && m_tileGrid->columnCount() == columns
        && m_tileGrid->itemAtPosition(0, columns - 1)) {
        return;
    }
    for (int index = 0; index < m_tiles.size(); ++index) {
        m_tileGrid->removeWidget(m_tiles.at(index));
    }
    for (int column = 0; column < 4; ++column) {
        m_tileGrid->setColumnStretch(column, column < columns ? 1 : 0);
    }
    for (int index = 0; index < m_tiles.size(); ++index) {
        m_tileGrid->addWidget(m_tiles.at(index), index / columns, index % columns);
    }
}

void HomePage::refreshLastTranscript()
{
    const QString transcript = m_controller->session()->lastTranscript().trimmed();
    m_lastRow->setVisible(!transcript.isEmpty());
    if (transcript.isEmpty()) return;
    m_lastText->setProperty("fullText", transcript);
    m_lastText->setText(twoLines(transcript, m_lastText->font(), std::max(1, m_lastText->width())));
    m_lastText->setToolTip(transcript);
    QString meta = wordCountText(countWords(transcript));
    if (const std::optional<DictationRecord> &record = m_controller->lastRecord()) {
        meta += QStringLiteral(", %1, %2").arg(
            record->appName, relativeDay(record->finishedAt.date(), m_controller->insightsToday()));
    }
    m_lastMeta->setText(meta);
}

void HomePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // The app can stay open past midnight; "today" has to move with it.
    if (m_summarizedDay != m_controller->insightsToday()) {
        refresh();
    } else {
        refreshLastTranscript();
    }
}

bool HomePage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Resize) {
        if (watched == m_column) {
            applyWidth();
        } else if (watched == m_lastText) {
            m_lastText->setText(twoLines(m_lastText->property("fullText").toString(),
                                         m_lastText->font(), m_lastText->width()));
        }
    }
    return QWidget::eventFilter(watched, event);
}

QPushButton *HomePage::toggleButton() const
{
    return m_toggle;
}

void HomePage::updateShortcutHint()
{
    // Without a Global Shortcut the hint is a link to where one is set.
    const QString shortcut = m_controller->globalShortcutDisplay();
    const QString hint = dictationShortcutHint(shortcut).toHtmlEscaped();
    m_hint->setText(shortcut.isEmpty() ? QStringLiteral("<a href=\"shortcut\">%1</a>").arg(hint) : hint);
}

void HomePage::setStatus(const QString &stateName)
{
    applyState(stateName);
    m_status->setText(dictationStatusLabel(stateName, m_controller->session()->lastMessage()));
}

void HomePage::applyState(const QString &stateName)
{
    const QString state = stateName.toCaseFolded();
    const bool active = dictationListeningPresentation(state);
    const DictationToggleAction toggle = dictationToggleAction(state);
    m_toggle->setText(toggle.label);
    m_toggle->setEnabled(toggle.enabled);
    m_toggle->setIcon(QIcon::fromTheme(active || state == QStringLiteral("refining")
                                           ? QStringLiteral("media-playback-stop")
                                           : QStringLiteral("media-record")));
    m_waveform->setVisible(active);
    const QString failure = dictationFailureNote(stateName, m_controller->session()->lastFailure());
    m_errorText->setText(failure);
    m_errorText->setVisible(!failure.isEmpty());
    if (!active) {
        m_waveform->setLevel(0.0f);
    }
}

} // namespace speecher
