#pragma once

#include "core/InsightsSummary.h"

#include <QWidget>

#include <array>

namespace speecher {

// Highlight mixed over Base: the one colour ramp every insights chart uses.
QColor accentTint(const QPalette &palette, int percent);

// Stock Qt Widgets has no heatmap, so this paints one (an approved exception to
// "the style draws"): rounded cells in palette colours, tooltips on hover.
class InsightsHeatmap final : public QWidget {
    Q_OBJECT

public:
    enum class Shape {
        Year,   // 53 Monday-first weeks, fewer when narrow; cells never under 10 px
        Week,   // this week's days as dots, today ringed
        Legend, // the five levels, Less to More
    };
    explicit InsightsHeatmap(Shape shape, QWidget *parent = nullptr);

    // Every day of the summary's heatmap, oldest first, ending today.
    void setDays(const QList<HeatmapDay> &days);
    void setMeasure(HeatMeasure measure);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    struct Cell {
        QRectF rect;
        int level = 0;
        bool ringed = false;
        QString tip;
    };
    struct Label {
        QPointF position;
        QString text;
    };
    struct Geometry {
        QList<Cell> cells;
        QList<Label> labels;
        QSize size;
    };

    Geometry layOut(int width) const;
    Geometry layOutYear(int width) const;
    Geometry layOutWeek() const;
    Geometry layOutLegend() const;
    QString describe(const HeatmapDay &day) const;
    QColor levelColor(int level) const;

    Shape m_shape;
    HeatMeasure m_measure = HeatMeasure::Dictations;
    QList<HeatmapDay> m_days;
    HeatScale m_scale{{}, HeatMeasure::Dictations};
};

// Dictations by hour of day: 24 bars, the peak in the accent colour. Painted
// for the same reason as the heatmap.
class InsightsBarChart final : public QWidget {
    Q_OBJECT

public:
    explicit InsightsBarChart(QWidget *parent = nullptr);

    void setCounts(const std::array<int, 24> &counts, int peakHour);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int barArea() const;
    QRectF slot(int hour) const;

    std::array<int, 24> m_counts{};
    int m_peakHour = 0;
};

} // namespace speecher
