#include "ui/HomePage.h"

#include "app/ApplicationController.h"
#include "core/InsightsLog.h"
#include "core/SettingsStore.h"
#include "dictation/DictationSession.h"
#include "ui/AccessibilityNotice.h"
#include "ui/WaveformWidget.h"
#include "ui/settings/SettingsPageSupport.h"

#include <QBuffer>
#include <QClipboard>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyle>
#include <QTextLayout>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace speecher {

namespace {

// Below this column width the two-up cards stack and the tiles go two by two.
// The column is capped like the settings cards, so a 1040 px window keeps
// both rows side by side and a 720 px one stacks them.
constexpr int kTwoUpMinimumWidth = 560;
// The lighter tint for every progress bar but the leading one. Breeze draws a
// progress fill darker than its palette colour, so the charts' 42 % mix would
// sink into the groove; this reads at the same step below the lead bar.
constexpr int kMutedProgressPercent = 70;

QString number(int value)
{
    return QLocale().toString(value);
}

QString plural(int count, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(number(count), count == 1 ? one : many);
}

QString capitalized(QString text)
{
    if (!text.isEmpty()) text[0] = text.at(0).toUpper();
    return text;
}

QIcon themedIcon(const QString &name, const QString &fallback = QString())
{
    return QIcon::fromTheme(name, QIcon::fromTheme(fallback));
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
QProgressBar *makeBar(int value, int maximum, bool leading, QWidget *parent)
{
    auto *bar = new QProgressBar(parent);
    bar->setRange(0, std::max(1, maximum));
    bar->setValue(value);
    bar->setTextVisible(false);
    bar->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    if (!leading) {
        // Qt 6 styles (Breeze among them) fill progress from Accent, older
        // ones from Highlight; the tint goes in both.
        const QColor tint = accentTint(bar->palette(), kMutedProgressPercent);
        QPalette tinted = bar->palette();
        tinted.setColor(QPalette::Highlight, tint);
        tinted.setColor(QPalette::Accent, tint);
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
        m_noticeButton = new QPushButton(QStringLiteral("Insights settings…"), host);
        m_noticeButton->setObjectName(QStringLiteral("insightsNoticeSettings"));
        row->addWidget(m_noticeButton, 0, Qt::AlignVCenter);
        noticeContent->addLayout(row);
        connect(m_noticeButton, &QPushButton::clicked, this,
                [this] { emit navigateRequested(AppPageId::General); });
    }
    m_columnLayout->addWidget(m_notice);

    m_insightsHeader = new QWidget(m_column);
    {
        auto *header = new QHBoxLayout(m_insightsHeader);
        header->setContentsMargins(0, settings::relatedSpacing(), 0, 0);
        header->addWidget(settings::makeSectionLabel(QStringLiteral("Your dictation"), m_insightsHeader),
                          1, Qt::AlignBottom);
        m_range = new QComboBox(m_insightsHeader);
        m_range->setObjectName(QStringLiteral("insightsRange"));
        m_range->setAccessibleName(QStringLiteral("Period"));
        m_range->addItem(QStringLiteral("Last 7 days"), int(InsightsRange::Last7Days));
        m_range->addItem(QStringLiteral("Last 30 days"), int(InsightsRange::Last30Days));
        m_range->addItem(QStringLiteral("This year"), int(InsightsRange::ThisYear));
        m_range->addItem(QStringLiteral("All time"), int(InsightsRange::AllTime));
        m_range->setCurrentIndex(1);
        header->addWidget(m_range);
        connect(m_range, &QComboBox::currentIndexChanged, this, &HomePage::refresh);
    }
    m_columnLayout->addWidget(m_insightsHeader);
    m_columnLayout->addStretch();

    connect(controller->insightsLog(), &InsightsLog::changed, this, &HomePage::refresh);
    connect(m_toggle, &QPushButton::clicked, controller, &ApplicationController::toggle);
    connect(controller, &ApplicationController::stateChanged, this, &HomePage::applyState);
    connect(controller, &ApplicationController::statusChanged, this, &HomePage::setDisplayStatus);
    connect(controller, &ApplicationController::audioLevelChanged, m_waveform, &WaveformWidget::setLevel);
    connect(controller, &ApplicationController::transcriptDelivered, this,
            &HomePage::refreshLastTranscript);
    connect(controller, &ApplicationController::lastRecordChanged, this,
            &HomePage::refreshLastTranscript);
    connect(controller->session(), &DictationSession::popupErrorRequested, this,
            [this](const QString &message) {
                m_errorText->setText(message.simplified());
                m_errorText->setVisible(!message.simplified().isEmpty());
            });
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
    text->addWidget(m_hint);
    // The popup shows a failure for five seconds and cannot take focus, so the
    // reason also stays here until the next session starts.
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
    m_copyTranscript->setToolTip(QStringLiteral("Copy transcript"));
    last->addWidget(m_copyTranscript, 0, Qt::AlignTop);
    connect(m_copyTranscript, &QToolButton::clicked, this, [this, copyIcon] {
        const QString transcript = m_controller->session()->lastTranscript();
        if (transcript.isEmpty()) return;
        QGuiApplication::clipboard()->setText(transcript);
        m_copyTranscript->setIcon(themedIcon(QStringLiteral("checkmark"),
                                             QStringLiteral("dialog-ok-apply")));
        m_copyTranscript->setText(QStringLiteral("Copied"));
        QTimer::singleShot(1500, m_copyTranscript, [this, copyIcon] {
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
    m_pairs.clear();

    const bool enabled = m_controller->settings()->insightsEnabled();
    const QList<DictationRecord> &records = m_controller->insightsLog()->records();
    auto *title = m_notice->findChild<QLabel *>(QStringLiteral("insightsNoticeTitle"));
    auto *body = m_notice->findChild<QLabel *>(QStringLiteral("insightsNoticeBody"));
    if (!enabled) {
        title->setText(QStringLiteral("Insights are off"));
        body->setText(QStringLiteral("Speecher isn't keeping any record of your dictation. If you "
                                     "turn insights on, your stats are stored only on this "
                                     "computer and never sent to the cloud."));
    } else {
        title->setText(QStringLiteral("No insights yet"));
        body->setText(QStringLiteral("Your stats appear here after your next dictation. They're "
                                     "stored only on this computer and never sent to the cloud."));
    }
    m_noticeButton->setVisible(!enabled);
    const bool showStats = enabled && !records.isEmpty();
    m_notice->setVisible(!showStats);
    m_insightsHeader->setVisible(showStats);
    if (showStats) {
        const auto range = static_cast<InsightsRange>(m_range->currentData().toInt());
        m_insights = buildInsights(summarize(records, range, m_controller->insightsToday()));
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
    layout->addWidget(settings::makeSectionLabel(QStringLiteral("Records"), insights));
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

    const auto tile = [this, host](const QString &iconName, const QString &label,
                                   QWidget *value, const QStringList &lines, const QString &tip) {
        QVBoxLayout *content = nullptr;
        QFrame *card = makeCard(host, &content);
        card->setObjectName(QStringLiteral("insightTile"));
        content->setSpacing(0);
        QWidget *cardHost = content->parentWidget();
        auto *title = new QHBoxLayout;
        title->setSpacing(settings::tightSpacing());
        const QIcon icon = QIcon::fromTheme(iconName);
        if (!icon.isNull()) {
            auto *iconLabel = new QLabel(cardHost);
            const int extent = style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, this);
            iconLabel->setPixmap(icon.pixmap(extent, extent));
            title->addWidget(iconLabel);
        }
        title->addWidget(mutedLabel(label, cardHost, false), 1);
        content->addLayout(title);
        value->setParent(cardHost);
        content->addWidget(value);
        for (int index = 0; index < lines.size(); ++index) {
            if (lines.at(index).isEmpty()) continue;
            QLabel *line = mutedLabel(lines.at(index), cardHost);
            if (index == 0) line->setToolTip(tip);
            content->addWidget(line);
        }
        content->addStretch();
        m_tiles.append(card);
        return content;
    };

    const QString period = summary.deltaPeriodLabel;
    tile(QStringLiteral("format-justify-left"), QStringLiteral("Words dictated"),
         bigNumber({{number(summary.words), {}}}, nullptr),
         {summary.bookComparison, deltaText(summary.wordsDelta, period)},
         summary.bookComparisonTip);

    QVBoxLayout *streak = tile(
        QStringLiteral("games-highscores"), QStringLiteral("Streak"),
        bigNumber({{number(summary.currentStreak),
                    summary.currentStreak == 1 ? QStringLiteral("day") : QStringLiteral("days")}},
                  nullptr),
        {streakText(summary, m_controller->insightsToday())}, {});
    auto *week = new InsightsHeatmap(InsightsHeatmap::Shape::Week, streak->parentWidget());
    week->setObjectName(QStringLiteral("streakWeek"));
    week->setDays(summary.heatmap);
    streak->insertSpacing(streak->count() - 1, settings::tightSpacing());
    streak->insertWidget(streak->count() - 1, week);

    tile(QStringLiteral("audio-input-microphone"), QStringLiteral("Dictations"),
         bigNumber({{number(summary.dictations), {}}}, nullptr),
         {summary.activeDays
              ? QStringLiteral("%1 a day when you dictate")
                    .arg(QLocale().toString(summary.dictationsPerActiveDay, 'f', 1))
              : QStringLiteral("Nothing yet"),
          deltaText(summary.dictationsDelta, period)},
         {});

    // "4.0 hours": the figure large, its unit small.
    const QString audio = audioTotalText(summary.audioMs);
    tile(QStringLiteral("waveform"), QStringLiteral("Audio transcribed"),
         bigNumber({{audio.section(u' ', 0, 0), audio.section(u' ', 1)}}, nullptr),
         {averageDictationText(summary)}, {});
    return host;
}

QFrame *HomePage::buildActivityCard(const InsightsSummary &summary, QWidget *parent)
{
    QVBoxLayout *content = nullptr;
    QFrame *card = makeCard(parent, &content);
    card->setObjectName(QStringLiteral("activityCard"));
    QWidget *host = content->parentWidget();
    auto *head = new QHBoxLayout;
    head->addWidget(boldLabel(QStringLiteral("Activity"), host), 1);
    auto *measure = new QComboBox(host);
    measure->setObjectName(QStringLiteral("activityMeasure"));
    measure->setAccessibleName(QStringLiteral("Measure"));
    measure->addItem(QStringLiteral("Dictations"), int(HeatMeasure::Dictations));
    measure->addItem(QStringLiteral("Words"), int(HeatMeasure::Words));
    measure->addItem(QStringLiteral("Minutes of audio"), int(HeatMeasure::Audio));
    measure->setCurrentIndex(measure->findData(int(m_measure)));
    head->addWidget(measure);
    content->addLayout(head);

    auto *heatmap = new InsightsHeatmap(InsightsHeatmap::Shape::Year, host);
    heatmap->setObjectName(QStringLiteral("activityHeatmap"));
    heatmap->setDays(summary.heatmap);
    heatmap->setMeasure(m_measure);
    content->addWidget(heatmap);
    connect(measure, &QComboBox::currentIndexChanged, heatmap, [this, measure, heatmap] {
        m_measure = static_cast<HeatMeasure>(measure->currentData().toInt());
        heatmap->setMeasure(m_measure);
    });

    auto *foot = new QHBoxLayout;
    foot->setSpacing(settings::relatedSpacing());
    foot->addWidget(mutedLabel(QStringLiteral("%1 with dictation in the last year")
                                   .arg(plural(summary.activeDaysLastYear, QStringLiteral("day"),
                                               QStringLiteral("days"))),
                               host),
                    1);
    foot->addWidget(mutedLabel(QStringLiteral("Less"), host));
    foot->addWidget(new InsightsHeatmap(InsightsHeatmap::Shape::Legend, host));
    foot->addWidget(mutedLabel(QStringLiteral("More"), host));
    content->addLayout(foot);
    return card;
}

QFrame *HomePage::buildHoursCard(const InsightsSummary &summary, QWidget *parent)
{
    QVBoxLayout *content = nullptr;
    QFrame *card = makeTitledCard(QStringLiteral("When you talk"), parent, &content);
    card->setObjectName(QStringLiteral("hoursCard"));
    QWidget *host = content->parentWidget();
    if (!summary.hasHourData) {
        content->addWidget(mutedLabel(
            QStringLiteral("After a few days of dictation this shows the hours you talk most."),
            host, false));
        content->addStretch();
        return card;
    }
    auto *verdict = new QLabel(host);
    verdict->setWordWrap(true);
    verdict->setTextFormat(Qt::RichText);
    verdict->setText(
        QStringLiteral("<b>%1.</b> <span style=\"color:%2\">You dictate most around %3, and %4s "
                       "are your busiest day.</span>")
            .arg(summary.persona.toHtmlEscaped(),
                 palette().color(QPalette::PlaceholderText).name(),
                 hourLabel(summary.peakHour),
                 QLocale().dayName(summary.busiestWeekday, QLocale::LongFormat)));
    content->addWidget(verdict);
    content->addStretch();
    auto *chart = new InsightsBarChart(host);
    chart->setObjectName(QStringLiteral("hoursChart"));
    chart->setCounts(summary.hourCounts, summary.peakHour);
    content->addWidget(chart);
    return card;
}

QFrame *HomePage::buildPaceCard(const InsightsSummary &summary, QWidget *parent)
{
    QVBoxLayout *content = nullptr;
    QFrame *card = makeTitledCard(QStringLiteral("Pace"), parent, &content);
    card->setObjectName(QStringLiteral("paceCard"));
    QWidget *host = content->parentWidget();
    if (summary.dictations == 0) {
        content->addWidget(mutedLabel(QStringLiteral("No dictation in this period."), host, false));
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
         QStringLiteral("Your speaking pace"));
    const int saved = summary.minutesSavedVersusTyping;
    QList<QPair<QString, QString>> savedParts;
    if (saved >= 60) savedParts.append({number(saved / 60), QStringLiteral("h")});
    if (saved < 60 || saved % 60) savedParts.append({number(saved % 60), QStringLiteral("min")});
    stat(bigNumber(savedParts, nullptr), QStringLiteral("Saved over typing"));
    split->addStretch();
    content->addLayout(split);
    content->addStretch();

    QGridLayout *grid = makeBarGrid(content);
    const int scale = std::max(summary.wordsPerMinute, 160);
    addBarRow(grid, new QLabel(QStringLiteral("You, speaking"), host),
              makeBar(summary.wordsPerMinute, scale, true, host), number(summary.wordsPerMinute));
    addBarRow(grid, new QLabel(QStringLiteral("Typical typing"), host),
              makeBar(summary.typingWordsPerMinute, scale, false, host),
              number(summary.typingWordsPerMinute));
    content->addWidget(mutedLabel(summary.speedupText, host));
    return card;
}

QFrame *HomePage::buildAppsCard(const InsightsSummary &summary, QWidget *parent)
{
    QVBoxLayout *content = nullptr;
    QFrame *card = makeTitledCard(QStringLiteral("Where your words go"), parent, &content);
    card->setObjectName(QStringLiteral("appsCard"));
    QWidget *host = content->parentWidget();
    if (summary.apps.isEmpty()) {
        content->addWidget(mutedLabel(QStringLiteral("No dictation in this period."), host, false));
        content->addStretch();
        return card;
    }
    QGridLayout *grid = makeBarGrid(content);
    const int most = summary.apps.first().words;
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
            QLabel *profile = mutedLabel(app.profileLabel, name);
            profile->setWordWrap(false);
            nameLayout->addWidget(profile, 0, Qt::AlignBaseline);
        }
        nameLayout->addStretch();
        addBarRow(grid, name, makeBar(app.words, most, index == 0, host),
                  QStringLiteral("%1%").arg(app.percent),
                  QStringLiteral("<b>%1</b><br>%2")
                      .arg(app.name.toHtmlEscaped(),
                           plural(app.words, QStringLiteral("word"), QStringLiteral("words"))));
    }
    content->addStretch();
    return card;
}

QFrame *HomePage::buildCorrectionsCard(QWidget *parent)
{
    QVBoxLayout *content = nullptr;
    QFrame *card = makeTitledCard(QStringLiteral("Corrections"), parent, &content);
    card->setObjectName(QStringLiteral("correctionsCard"));
    QWidget *host = content->parentWidget();
    const int learned = m_controller->settings()->learnedCorrections().size();
    auto *stat = new QVBoxLayout;
    stat->setSpacing(0);
    stat->addWidget(bigNumber({{number(learned), {}}}, host));
    stat->addWidget(mutedLabel(learned == 1 ? QStringLiteral("Correction learned")
                                            : QStringLiteral("Corrections learned"),
                               host));
    content->addLayout(stat);
    content->addWidget(mutedLabel(
        QStringLiteral("Speecher learned these from edits you made after dictating."), host, false));
    content->addStretch();
    auto *open = new QPushButton(QStringLiteral("Review Corrections…"), host);
    open->setObjectName(QStringLiteral("reviewCorrections"));
    connect(open, &QPushButton::clicked, this, &HomePage::correctionsRequested);
    content->addWidget(open, 0, Qt::AlignLeft);
    return card;
}

QFrame *HomePage::buildRecordsCard(const InsightsSummary &summary, QWidget *parent)
{
    QFrame *card = settings::makeSettingsCard(parent);
    card->setObjectName(QStringLiteral("recordsCard"));
    QFormLayout *form = settings::cardFormLayout(card);
    QWidget *host = form->parentWidget();
    const QDate today = m_controller->insightsToday();
    const auto add = [form, host](const QString &title, const QString &description, QWidget *value) {
        settings::addCardRow(form, settings::makeRow(title, description, value, host), host);
    };
    const auto text = [host](const QString &value) { return new QLabel(value, host); };

    if (summary.nextMilestone > 0) {
        auto *progress = new QProgressBar(host);
        progress->setObjectName(QStringLiteral("milestoneProgress"));
        progress->setRange(0, summary.nextMilestone);
        progress->setValue(summary.allTimeWords);
        progress->setTextVisible(false);
        progress->setFixedWidth(settings::gridUnit() * 6);
        progress->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        add(QStringLiteral("Next milestone: %1 words").arg(number(summary.nextMilestone)),
            milestoneText(summary), progress);
    } else {
        add(QStringLiteral("Every milestone passed"), milestoneText(summary),
            text(plural(summary.allTimeWords, QStringLiteral("word"), QStringLiteral("words"))));
    }
    add(QStringLiteral("Longest streak"),
        summary.bestStreakEndsToday
            ? QStringLiteral("That's the one you're on")
            : QStringLiteral("Ended %1").arg(relativeDay(summary.bestStreakEnd, today)),
        text(plural(summary.bestStreak, QStringLiteral("day"), QStringLiteral("days"))));
    add(QStringLiteral("Longest dictation"),
        QStringLiteral("%1 words into %2, %3")
            .arg(number(summary.longest.words), summary.longest.appName,
                 relativeDay(summary.longest.date, today)),
        text(clockText(summary.longest.audioMs)));
    add(QStringLiteral("Busiest day"), capitalized(relativeDay(summary.busiestDay.date, today)),
        text(plural(summary.busiestDay.dictations, QStringLiteral("dictation"),
                    QStringLiteral("dictations"))));
    add(QStringLiteral("Wordiest day"), capitalized(relativeDay(summary.wordiestDay.date, today)),
        text(plural(summary.wordiestDay.words, QStringLiteral("word"), QStringLiteral("words"))));
    const int daysAgo = summary.firstDictation.daysTo(today);
    add(QStringLiteral("First dictation"),
        QLocale().toString(summary.firstDictation, QStringLiteral("MMM d, yyyy")),
        text(daysAgo == 0 ? QStringLiteral("Today")
                          : QStringLiteral("%1 ago").arg(plural(daysAgo, QStringLiteral("day"),
                                                                QStringLiteral("days")))));
    return card;
}

QWidget *HomePage::buildFooter(QWidget *parent)
{
    // One centred paragraph with the lock icon inline, so a narrow window
    // wraps it as a whole.
    QString lock;
    const QIcon lockIcon = themedIcon(QStringLiteral("object-locked"), QStringLiteral("lock"));
    if (!lockIcon.isNull()) {
        const int extent = QFontMetrics(settings::smallFont(font())).ascent();
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        lockIcon.pixmap(extent, extent).save(&buffer, "PNG");
        lock = QStringLiteral("<img src=\"data:image/png;base64,%1\" width=\"%2\" height=\"%2\"> ")
                   .arg(QString::fromLatin1(png.toBase64()))
                   .arg(extent);
    }
    QLabel *note = mutedLabel(
        lock + QStringLiteral("Insights are stored only on this computer and are never sent to "
                              "the cloud. <a href=\"general\">Insights settings</a>"),
        parent);
    note->setObjectName(QStringLiteral("insightsPrivacyNote"));
    note->setTextFormat(Qt::RichText);
    note->setAlignment(Qt::AlignHCenter);
    note->setContentsMargins(0, settings::relatedSpacing(), 0, 0);
    connect(note, &QLabel::linkActivated, this,
            [this] { emit navigateRequested(AppPageId::General); });
    return note;
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
    const int columns = width < kTwoUpMinimumWidth ? 2 : 4;
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
    QString meta = plural(countWords(transcript), QStringLiteral("word"), QStringLiteral("words"));
    if (const std::optional<DictationRecord> &record = m_controller->lastRecord()) {
        meta += QStringLiteral(", %1, %2").arg(
            record->appName, relativeDay(record->finishedAt.date(), m_controller->insightsToday()));
    }
    m_lastMeta->setText(meta);
}

void HomePage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refreshLastTranscript();
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

QScrollArea *HomePage::scrollArea() const
{
    return m_scroll;
}

void HomePage::updateShortcutHint()
{
    const QString shortcut = m_controller->globalShortcutDisplay();
    m_hint->setText(shortcut.isEmpty()
                        ? QStringLiteral("Set a Global Shortcut to dictate from anywhere.")
                        : QStringLiteral("Press %1 anywhere to dictate into the app you're using.")
                              .arg(shortcut));
}

void HomePage::applyToggleState(bool active, bool refining, const QString &state) const
{
    const bool busy = state == QStringLiteral("stopping") || state == QStringLiteral("delivering");
    m_toggle->setEnabled(!busy);
    m_toggle->setText(active                               ? QStringLiteral("Stop Dictation")
                      : refining                           ? QStringLiteral("Cancel Refinement")
                      : state == QStringLiteral("stopping")   ? QStringLiteral("Stopping…")
                      : state == QStringLiteral("delivering") ? QStringLiteral("Delivering…")
                                                           : QStringLiteral("Start Dictation"));
    m_toggle->setIcon(QIcon::fromTheme(active || refining ? QStringLiteral("media-playback-stop")
                                                          : QStringLiteral("media-record")));
}

void HomePage::setStatus(const QString &status)
{
    applyState(status);
    setDisplayStatus(status);
}

void HomePage::applyState(const QString &stateName)
{
    const QString state = stateName.toCaseFolded();
    const bool active = state == QStringLiteral("starting") || state == QStringLiteral("listening");
    const bool refining = state == QStringLiteral("refining");
    applyToggleState(active, refining, state);
    m_waveform->setVisible(active);
    if (active && !m_sessionActive) {
        m_errorText->clear();
        m_errorText->hide();
    }
    m_sessionActive = active;
    if (!active) {
        m_waveform->setLevel(0.0f);
    }
}

void HomePage::setDisplayStatus(const QString &status)
{
    const QString state = status.toCaseFolded();
    static const QStringList states{
        QStringLiteral("idle"),     QStringLiteral("starting"),   QStringLiteral("listening"),
        QStringLiteral("stopping"), QStringLiteral("refining"),   QStringLiteral("delivering"),
        QStringLiteral("error"),
    };
    m_status->setText(states.contains(state) ? state.left(1).toUpper() + state.mid(1) : status);
}

} // namespace speecher
