#pragma once

#include "core/InsightsSummary.h"
#include "ui/AppPage.h"
#include "ui/InsightsCharts.h"

#include <QWidget>

class QBoxLayout;
class QComboBox;
class QFrame;
class QGridLayout;
class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace speecher {

class AccessibilityNotice;
class ApplicationController;
class WaveformWidget;

// Home: the dictation card, then what the insights log says about your
// dictation (see InsightsSummary), or a note when there is nothing to show.
class HomePage : public QWidget {
    Q_OBJECT

public:
    explicit HomePage(ApplicationController *controller, QWidget *parent = nullptr);
    QPushButton *toggleButton() const;
    QScrollArea *scrollArea() const;

public slots:
    void setStatus(const QString &status);
    // Rebuilds the insights from the log and the Insights setting.
    void refresh();

signals:
    void navigateRequested(AppPageId page);
    void correctionsRequested();

protected:
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QFrame *buildDictationCard(QWidget *parent);
    QWidget *buildInsights(const InsightsSummary &summary);
    QWidget *buildTiles(const InsightsSummary &summary, QWidget *parent);
    QFrame *buildActivityCard(const InsightsSummary &summary, QWidget *parent);
    QFrame *buildHoursCard(const InsightsSummary &summary, QWidget *parent);
    QFrame *buildPaceCard(const InsightsSummary &summary, QWidget *parent);
    QFrame *buildAppsCard(const InsightsSummary &summary, QWidget *parent);
    QFrame *buildCorrectionsCard(QWidget *parent);
    QFrame *buildRecordsCard(const InsightsSummary &summary, QWidget *parent);
    QWidget *buildFooter(QWidget *parent);
    void applyWidth();
    void refreshLastTranscript();
    void applyState(const QString &stateName);
    void setDisplayStatus(const QString &status);
    void updateShortcutHint();
    void applyToggleState(bool active, bool refining, const QString &state) const;

    ApplicationController *m_controller;
    QScrollArea *m_scroll;
    QWidget *m_column;
    AccessibilityNotice *m_accessibilityNotice;
    QPushButton *m_toggle = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_errorText = nullptr;
    WaveformWidget *m_waveform = nullptr;
    QWidget *m_lastRow = nullptr;
    QLabel *m_lastText = nullptr;
    QLabel *m_lastMeta = nullptr;
    QToolButton *m_copyTranscript = nullptr;
    // "Insights are off" or "No insights yet"; hidden while there are stats.
    QFrame *m_notice = nullptr;
    QPushButton *m_noticeButton = nullptr;
    QWidget *m_insightsHeader = nullptr;
    QComboBox *m_range = nullptr;
    QWidget *m_insights = nullptr;
    QVBoxLayout *m_columnLayout = nullptr;
    // What applyWidth rearranges when the column is narrow.
    QGridLayout *m_tileGrid = nullptr;
    QList<QWidget *> m_tiles;
    QList<QBoxLayout *> m_pairs;
    HeatMeasure m_measure = HeatMeasure::Dictations;
    bool m_sessionActive = false;
};

} // namespace speecher
