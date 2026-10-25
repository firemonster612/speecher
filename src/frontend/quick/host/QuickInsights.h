#pragma once

#include "core/InsightsSummary.h"

#include <QDate>
#include <QImage>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QUrl>
#include <QVariant>

namespace speecher {

class ApplicationController;

// The UI's Insights singleton: Home's statistics, from core's insights
// summary for the chosen period; the history General counts and clears; the
// latest transcript; the learned corrections Home lists; and the exports.
// It carries data only: Home and General word it. Dates reach QML as Dates
// at the start of their local day, and an absent one as null.
class QuickInsights final : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Insights)
    QML_SINGLETON
    // ready, empty (nothing kept, or cleared) or off (Keep insights is off;
    // what was kept stays until cleared).
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool hasData READ hasData NOTIFY changed)
    // The dictations the log keeps, whether or not Insights is on, and the
    // day of the first (null with none).
    Q_PROPERTY(int kept READ kept NOTIFY changed)
    Q_PROPERTY(QVariant firstDictation READ firstDictation NOTIFY changed)
    // 7d, 30d, year or all; the figures, hours, apps and profiles cover it.
    Q_PROPERTY(QString period READ period WRITE setPeriod NOTIFY periodChanged)
    // dictations, words or audio: what heatLevels colours the days by.
    Q_PROPERTY(QString heatMeasure READ heatMeasure WRITE setHeatMeasure NOTIFY heatMeasureChanged)
    // The day the summary counts up to. It moves at midnight.
    Q_PROPERTY(QVariant today READ today NOTIFY changed)
    // While hasData, the period's numbers and the all-time ones; else null.
    // QuickInsights.cpp's summaryMap lists the fields.
    Q_PROPERTY(QVariant summary READ summary NOTIFY changed)
    // Every day of the last 53 Monday-first weeks up to today, oldest first:
    // { date, dictations, words, audioMs }. Empty without data.
    Q_PROPERTY(QVariantList heatmap READ heatmap NOTIFY changed)
    // Each heatmap day's colour level for heatMeasure: 0 for no dictation,
    // then 1 to 4 by the quartile of its value among the days with some.
    Q_PROPERTY(QVariantList heatLevels READ heatLevels NOTIFY heatLevelsChanged)
    // The locale's short month names over the heatmap's week columns:
    // [{ column, label }].
    Q_PROPERTY(QVariantList monthLabels READ monthLabels NOTIFY changed)
    // { count, learning (correction learning is on), accessible (the
    // desktop lets Speecher see edits), recent: the newest three as
    // [{ heard, fixed }] }.
    Q_PROPERTY(QVariantMap corrections READ corrections NOTIFY correctionsChanged)
    // The latest transcript, kept in memory only: { text, words, app,
    // profile, date }, where app, profile and date are empty or null unless
    // insights recorded it. Null before the first.
    Q_PROPERTY(QVariant latest READ latest NOTIFY latestChanged)

public:
    explicit QuickInsights(ApplicationController *controller, QObject *parent = nullptr);
    ~QuickInsights() override;

    // The engine's instance: the one the host made before loading any QML.
    static QuickInsights *create(QQmlEngine *, QJSEngine *);

    QString state() const;
    bool hasData() const;
    int kept() const;
    QVariant firstDictation() const;
    QString period() const;
    void setPeriod(const QString &period);
    QString heatMeasure() const;
    void setHeatMeasure(const QString &measure);
    QVariant today() const;
    QVariant summary() const;
    QVariantList heatmap() const;
    QVariantList heatLevels() const;
    QVariantList monthLabels() const;
    QVariantMap corrections() const;
    QVariant latest() const;

    // Deletes the kept history. False, with the history kept, when its file
    // could not be deleted.
    Q_INVOKABLE bool clear();
    // Puts Home's share text, which the page words, on the clipboard.
    Q_INVOKABLE void copyText(const QString &text);
    // Puts Home's share picture, which the page draws, on the clipboard.
    // False, with nothing copied, for an empty picture.
    Q_INVOKABLE bool copyImage(const QImage &image);
    // Writes the period's numbers as JSON to a local file the page chose,
    // replacing it. False when it could not be written.
    Q_INVOKABLE bool saveJson(const QUrl &file);
    // The latest transcript, on the clipboard.
    Q_INVOKABLE void copyLatest();

signals:
    void changed();
    void periodChanged();
    void heatMeasureChanged();
    void heatLevelsChanged();
    void correctionsChanged();
    void latestChanged();

private:
    void refresh();
    void refreshCorrections();
    void scheduleMidnight();

    ApplicationController *m_controller;
    InsightsRange m_range = InsightsRange::Last30Days;
    HeatMeasure m_measure = HeatMeasure::Dictations;
    QDate m_today;
    InsightsSummary m_summary;
    QVariant m_summaryMap;
    QVariantList m_heatmap;
    QVariantMap m_corrections;
    QTimer m_midnight;
};

} // namespace speecher
