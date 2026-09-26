#include "ui/InsightsCharts.h"

#include "ui/settings/SettingsPageSupport.h"

#include <QHelpEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <algorithm>

namespace speecher {

namespace {

// Chart geometry from the design: cells between 10 and 14 px with 3 px gaps,
// 2 px between hour bars.
constexpr int kWeeks = 53;
constexpr int kMaxCell = 14;
constexpr int kMinCell = 10;
constexpr int kCellGap = 3;
constexpr int kDot = 10;
constexpr int kBarGap = 2;
constexpr int kMutedBarPercent = 42;
// The badge's fill: the heatmap's lightest active level, so it reads as a
// tag in the same ramp without competing with the bars beside it.
constexpr int kBadgePercent = 30;

QColor mix(const QColor &from, const QColor &to, int percent)
{
    const auto channel = [percent](int a, int b) { return a + (b - a) * percent / 100; };
    return QColor(channel(from.red(), to.red()),
                  channel(from.green(), to.green()),
                  channel(from.blue(), to.blue()));
}

QString plural(int count, const QString &one, const QString &many)
{
    return QStringLiteral("%1 %2").arg(QLocale().toString(count), count == 1 ? one : many);
}

QString audioText(qint64 audioMs)
{
    const qint64 seconds = (audioMs + 500) / 1000;
    if (seconds < 60) return QStringLiteral("%1s").arg(seconds);
    const qint64 minutes = (seconds + 30) / 60;
    if (minutes < 60) return QStringLiteral("%1 min").arg(minutes);
    return minutes % 60 ? QStringLiteral("%1 h %2 min").arg(minutes / 60).arg(minutes % 60)
                        : QStringLiteral("%1 h").arg(minutes / 60);
}

QString dayText(const QDate &date)
{
    return QLocale().toString(date, QStringLiteral("ddd, MMM d, yyyy"));
}

} // namespace

QColor accentTint(const QPalette &palette, int percent)
{
    return mix(palette.color(QPalette::Base), palette.color(QPalette::Highlight), percent);
}

InsightsHeatmap::InsightsHeatmap(Shape shape, QWidget *parent)
    : QWidget(parent)
    , m_shape(shape)
{
    QSizePolicy policy(shape == Shape::Year ? QSizePolicy::Expanding : QSizePolicy::Fixed,
                       QSizePolicy::Fixed);
    policy.setHeightForWidth(shape == Shape::Year);
    setSizePolicy(policy);
    setMouseTracking(true);
}

void InsightsHeatmap::setDays(const QList<HeatmapDay> &days)
{
    m_days = days;
    setMeasure(m_measure);
}

void InsightsHeatmap::setMeasure(HeatMeasure measure)
{
    m_measure = measure;
    m_scale = HeatScale(m_days, measure);
    updateGeometry();
    update();
}

QString InsightsHeatmap::describe(const HeatmapDay &day) const
{
    if (day.dictations == 0) return QStringLiteral("No dictation");
    switch (m_measure) {
    case HeatMeasure::Words:
        return QStringLiteral("%1 from %2").arg(plural(day.words, QStringLiteral("word"), QStringLiteral("words")),
                                               plural(day.dictations, QStringLiteral("dictation"), QStringLiteral("dictations")));
    case HeatMeasure::Audio:
        return QStringLiteral("%1 of audio").arg(audioText(day.audioMs));
    case HeatMeasure::Dictations: break;
    }
    return QStringLiteral("%1, %2").arg(plural(day.dictations, QStringLiteral("dictation"), QStringLiteral("dictations")),
                                        plural(day.words, QStringLiteral("word"), QStringLiteral("words")));
}

QColor InsightsHeatmap::levelColor(int level) const
{
    if (level == 0) {
        // No activity: a faint trace of the text colour on the card.
        return mix(palette().color(QPalette::Base), palette().color(QPalette::Text), 8);
    }
    return accentTint(palette(), qRound(kHeatStrengths[level] * 100));
}

InsightsHeatmap::Geometry InsightsHeatmap::layOut(int width) const
{
    switch (m_shape) {
    case Shape::Week: return layOutWeek();
    case Shape::Legend: return layOutLegend();
    case Shape::Year: break;
    }
    return layOutYear(width);
}

InsightsHeatmap::Geometry InsightsHeatmap::layOutYear(int width) const
{
    Geometry geometry;
    const QFontMetrics metrics(font());
    const int labelWidth = metrics.horizontalAdvance(QStringLiteral("Wed")) + settings::relatedSpacing();
    const int labelHeight = metrics.height() + settings::tightSpacing();
    int weeks = kWeeks;
    int cell = std::min(kMaxCell, (width - labelWidth) / weeks - kCellGap);
    if (cell < kMinCell) {
        cell = kMinCell;
        weeks = std::max(1, (width - labelWidth) / (cell + kCellGap));
    }
    const int pitch = cell + kCellGap;
    geometry.size = QSize(labelWidth + weeks * pitch - kCellGap, labelHeight + 7 * pitch - kCellGap);
    if (m_days.isEmpty()) return geometry;

    const QDate today = m_days.last().date;
    const QDate firstMonday = today.addDays(-(today.dayOfWeek() - 1) - (weeks - 1) * 7);
    const QDate oldest = m_days.first().date;
    const QMap<int, QString> months = monthLabels(m_days, weeks);
    for (auto month = months.cbegin(); month != months.cend(); ++month) {
        geometry.labels.append({QPointF(labelWidth + month.key() * pitch, metrics.ascent()), month.value()});
    }
    for (int week = 0; week < weeks; ++week) {
        const QDate monday = firstMonday.addDays(week * 7);
        const qreal x = labelWidth + week * pitch;
        for (int row = 0; row < 7; ++row) {
            const QDate date = monday.addDays(row);
            if (date > today || date < oldest) continue;
            const HeatmapDay &day = m_days.at(oldest.daysTo(date));
            geometry.cells.append({QRectF(x, labelHeight + row * pitch, cell, cell),
                                   m_scale.level(day),
                                   false,
                                   QStringLiteral("<b>%1</b><br>%2").arg(describe(day), dayText(date))});
        }
    }
    const QStringList rows{QStringLiteral("Mon"), QStringLiteral("Wed"), QStringLiteral("Fri")};
    for (int index = 0; index < rows.size(); ++index) {
        const int row = index * 2;
        geometry.labels.append({QPointF(0, labelHeight + row * pitch + (cell + metrics.ascent() - metrics.descent()) / 2.0),
                                rows.at(index)});
    }
    return geometry;
}

InsightsHeatmap::Geometry InsightsHeatmap::layOutWeek() const
{
    Geometry geometry;
    const QFontMetrics metrics(font());
    const int pitch = std::max(kDot, metrics.horizontalAdvance(QLatin1Char('W'))) + settings::tightSpacing();
    const int ring = 2;
    geometry.size = QSize(7 * pitch, kDot + 2 * ring + settings::tightSpacing() / 2 + metrics.height());
    const QDate today = m_days.isEmpty() ? QDate() : m_days.last().date;
    const int todayIndex = today.isValid() ? today.dayOfWeek() - 1 : -1;
    const QLocale locale;
    for (int index = 0; index < 7; ++index) {
        const qreal centre = index * pitch + pitch / 2.0;
        const QString letter = locale.dayName(index + 1, QLocale::NarrowFormat);
        geometry.labels.append({QPointF(centre - metrics.horizontalAdvance(letter) / 2.0,
                                        geometry.size.height() - metrics.descent()),
                                letter});
        // Days still to come this week get a letter and no dot.
        if (index > todayIndex) continue;
        const HeatmapDay &day = m_days.at(m_days.size() - 1 - (todayIndex - index));
        geometry.cells.append({QRectF(centre - kDot / 2.0, ring, kDot, kDot),
                               day.dictations ? 4 : 0,
                               index == todayIndex,
                               QStringLiteral("%1<br>%2").arg(
                                   dayText(day.date),
                                   day.dictations ? plural(day.dictations, QStringLiteral("dictation"), QStringLiteral("dictations"))
                                                  : QStringLiteral("No dictation"))});
    }
    return geometry;
}

InsightsHeatmap::Geometry InsightsHeatmap::layOutLegend() const
{
    Geometry geometry;
    const int pitch = kMinCell + kCellGap;
    geometry.size = QSize(5 * pitch - kCellGap, kMinCell);
    for (int level = 0; level < 5; ++level) {
        geometry.cells.append({QRectF(level * pitch, 0, kMinCell, kMinCell), level, false, {}});
    }
    return geometry;
}

QSize InsightsHeatmap::sizeHint() const
{
    // The year's natural size is every week at the largest cell.
    return layOut(QFontMetrics(font()).horizontalAdvance(QStringLiteral("Wed"))
                  + settings::relatedSpacing() + kWeeks * (kMaxCell + kCellGap))
        .size;
}

QSize InsightsHeatmap::minimumSizeHint() const
{
    if (m_shape != Shape::Year) return sizeHint();
    // A few months at the smallest cell; narrower windows scroll the page.
    return layOut(0).size.expandedTo(QSize(20 * (kMinCell + kCellGap), 0));
}

bool InsightsHeatmap::hasHeightForWidth() const
{
    return m_shape == Shape::Year;
}

int InsightsHeatmap::heightForWidth(int width) const
{
    return layOut(width).size.height();
}

bool InsightsHeatmap::event(QEvent *event)
{
    // Hover shows the tip at once (mouseMoveEvent); the delayed tooltip event
    // would only repeat it late, or pop a stale one after the pointer left.
    if (event->type() == QEvent::ToolTip) {
        event->ignore();
        return true;
    }
    return QWidget::event(event);
}

void InsightsHeatmap::mouseMoveEvent(QMouseEvent *event)
{
    const QList<Cell> cells = layOut(width()).cells;
    int hovered = -1;
    for (int index = 0; index < cells.size(); ++index) {
        // The gap between cells belongs to a cell, so the tip does not flicker
        // off while the pointer crosses it.
        const qreal pad = kCellGap / 2.0;
        if (!cells.at(index).tip.isEmpty()
            && cells.at(index).rect.adjusted(-pad, -pad, pad, pad).contains(event->position())) {
            hovered = index;
            break;
        }
    }
    if (hovered == m_hovered) return;
    m_hovered = hovered;
    update();
    if (hovered < 0) {
        QToolTip::hideText();
        return;
    }
    const Cell &cell = cells.at(hovered);
    // The tip's area is the whole hover area, gap included: Qt hides a tip
    // once the pointer rests outside the rect it was given.
    const qreal pad = kCellGap / 2.0;
    QToolTip::showText(event->globalPosition().toPoint(), cell.tip, this,
                       cell.rect.adjusted(-pad, -pad, pad, pad).toAlignedRect());
}

void InsightsHeatmap::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (m_hovered < 0) return;
    m_hovered = -1;
    QToolTip::hideText();
    update();
}

void InsightsHeatmap::paintEvent(QPaintEvent *)
{
    const Geometry geometry = layOut(width());
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    for (const Cell &cell : geometry.cells) {
        painter.setBrush(levelColor(cell.level));
        const qreal radius = m_shape == Shape::Week ? cell.rect.width() / 2 : 2;
        painter.drawRoundedRect(cell.rect, radius, radius);
        if (cell.ringed) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(palette().color(QPalette::Highlight), 1.5));
            painter.drawEllipse(cell.rect.adjusted(-2, -2, 2, 2));
            painter.setPen(Qt::NoPen);
        }
    }
    // The hovered cell is outlined in the text colour, which reads on every
    // level from empty to full accent.
    if (m_hovered >= 0 && m_hovered < geometry.cells.size()) {
        const QRectF hovered = geometry.cells.at(m_hovered).rect.adjusted(-0.5, -0.5, 0.5, 0.5);
        const qreal radius = m_shape == Shape::Week ? hovered.width() / 2 : 2.5;
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(palette().color(QPalette::Text), 1.5));
        painter.drawRoundedRect(hovered, radius, radius);
        painter.setPen(Qt::NoPen);
    }
    painter.setPen(palette().color(QPalette::PlaceholderText));
    for (const Label &label : geometry.labels) {
        painter.drawText(label.position, label.text);
    }
}

InsightsBarChart::InsightsBarChart(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void InsightsBarChart::setCounts(const std::array<int, 24> &counts, int peakHour)
{
    m_counts = counts;
    m_peakHour = peakHour;
    update();
}

int InsightsBarChart::barArea() const
{
    return height() - fontMetrics().height() - settings::tightSpacing();
}

QRectF InsightsBarChart::slot(int hour) const
{
    const qreal barWidth = (width() - kBarGap * 23.0) / 24.0;
    return QRectF(hour * (barWidth + kBarGap), 0, barWidth, barArea());
}

QSize InsightsBarChart::sizeHint() const
{
    return QSize(24 * 12, settings::gridUnit() * 4 + fontMetrics().height() + settings::tightSpacing());
}

QSize InsightsBarChart::minimumSizeHint() const
{
    return QSize(24 * 4, sizeHint().height());
}

bool InsightsBarChart::event(QEvent *event)
{
    // As on the heatmap: hover describes a bar at once, so the delayed
    // tooltip event has nothing to add.
    if (event->type() == QEvent::ToolTip) {
        event->ignore();
        return true;
    }
    return QWidget::event(event);
}

int InsightsBarChart::hourAt(const QPointF &position) const
{
    for (int hour = 0; hour < 24; ++hour) {
        if (slot(hour).adjusted(0, 0, kBarGap, 0).contains(position)) return hour;
    }
    return -1;
}

void InsightsBarChart::showHour(int hour, const QPoint &globalPosition)
{
    QToolTip::showText(globalPosition,
                       QStringLiteral("<b>%1 to %2</b><br>%3")
                           .arg(hourLabel(hour), hourLabel((hour + 1) % 24),
                                plural(m_counts[hour], QStringLiteral("dictation"),
                                       QStringLiteral("dictations"))),
                       this, slot(hour).adjusted(0, 0, kBarGap, 0).toAlignedRect());
}

void InsightsBarChart::mouseMoveEvent(QMouseEvent *event)
{
    const int hour = hourAt(event->position());
    if (hour == m_hovered) return;
    m_hovered = hour;
    update();
    if (hour < 0) {
        QToolTip::hideText();
        return;
    }
    showHour(hour, event->globalPosition().toPoint());
}

void InsightsBarChart::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (m_hovered < 0) return;
    m_hovered = -1;
    QToolTip::hideText();
    update();
}

void InsightsBarChart::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const int area = barArea();
    const int most = std::max(1, *std::max_element(m_counts.begin(), m_counts.end()));
    painter.setPen(Qt::NoPen);
    for (int hour = 0; hour < 24; ++hour) {
        if (m_counts[hour] == 0) continue;
        const QRectF box = slot(hour);
        const qreal height = std::max(3.0, m_counts[hour] * (area - 4.0) / most);
        // A hovered bar takes the full accent, as the peak does.
        painter.setBrush(hour == m_peakHour || hour == m_hovered
                             ? palette().color(QPalette::Highlight)
                             : accentTint(palette(), kMutedBarPercent));
        painter.drawRoundedRect(QRectF(box.left(), area - height, box.width(), height), 2, 2);
    }
    painter.setPen(QPen(settings::frameColor(palette()), 1));
    painter.drawLine(QPointF(0, area + 0.5), QPointF(width(), area + 0.5));
    painter.setPen(palette().color(QPalette::PlaceholderText));
    for (int hour : {0, 6, 12, 18}) {
        painter.drawText(QPointF(slot(hour).left(), height() - fontMetrics().descent()), hourLabel(hour));
    }
}

InsightsBadge::InsightsBadge(const QString &text, QWidget *parent)
    : QWidget(parent)
    , m_text(text)
{
    setFont(settings::smallFont(font()));
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAccessibleName(text);
}

QSize InsightsBadge::sizeHint() const
{
    // Padding from the text's own height, so the pill scales with the font.
    const QFontMetrics metrics = fontMetrics();
    const int vertical = metrics.height() / 6;
    return {metrics.horizontalAdvance(m_text) + metrics.height(), metrics.height() + 2 * vertical};
}

QSize InsightsBadge::minimumSizeHint() const
{
    return sizeHint();
}

void InsightsBadge::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF pill = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = pill.height() / 2;
    painter.setPen(Qt::NoPen);
    painter.setBrush(accentTint(palette(), kBadgePercent));
    painter.drawRoundedRect(pill, radius, radius);
    painter.setPen(palette().color(QPalette::Text));
    painter.drawText(rect(), Qt::AlignCenter, m_text);
}

} // namespace speecher
