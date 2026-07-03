#pragma once

// Module 3: the calendar. A week time-grid (7 day columns x 24h) fit to the
// viewport. Renders planned blocks plus phantom occurrences expanded from
// recurring series; supports drag-to-create, drag-to-move, and edge-resize,
// snapping to 15 minutes. Editing a phantom occurrence materializes it first.
//
// CalGrid is the painted grid + interaction; CalendarView wraps it with a
// navigation toolbar (prev / today / next + week label).

#include <QColor>
#include <QDate>
#include <QDateTime>
#include <QGraphicsView>
#include <QHash>
#include <QWidget>

#include "storage/types.h"

class QGraphicsScene;
class QLabel;

namespace zb {

class Store;

class CalGrid : public QGraphicsView {
    Q_OBJECT
public:
    explicit CalGrid(Store& store, QWidget* parent = nullptr);

    void  setWeekStart(const QDate& monday);
    QDate weekStart() const { return m_weekStart; }
    void  setActiveProject(Id p) { m_activeProject = p; }
    void  reload();
    int   occurrenceCount() const { return m_occ.size(); } // for tests
    double allDayBandHeight() const { return m_allDayBandH; } // for tests

signals:
    void weekChanged();

protected:
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override; // Esc cancels an active drag

private:
    // --- layout math (all in viewport == scene coordinates) -----------------
    static constexpr double kGutter = 54.0; // hour-label column
    static constexpr double kHeader = 34.0; // day-header band
    // All-day band (between the header and the time grid): read-only external
    // all-day events stack here, one row per event, capped with a "+N" chip.
    static constexpr double kAllDayRowH = 18.0;
    static constexpr double kAllDayPad = 3.0;
    static constexpr int    kMaxAllDayRows = 3;
    // Each day column splits into a planned lane (left) and an actual lane
    // (right) — the plan-vs-actual view.
    enum class Lane { Planned, Actual };

    double dayWidth() const;
    double laneWidth() const { return dayWidth() / 2.0; }
    double ppm() const;                 // pixels per minute
    double xForDay(int d) const;
    double xForLane(int d, Lane lane) const;
    // Top of the time grid: header + the (data-driven) all-day band height.
    double gridTop() const { return kHeader + m_allDayBandH; }
    double yForMinutes(double m) const;
    int    dayAt(double x) const;       // 0..6, or -1 in the gutter
    double minutesAt(double y) const;   // 0..1440 (unsnapped)
    double snap(double minutes) const;  // to the configured snap interval
    QRectF rectForSpan(const QDateTime& s, const QDateTime& e, Lane lane) const;
    // Topmost occurrence under pos. Fills edge (-1 top,0 body,1 bottom) and the
    // lane hit. Returns -1 on miss.
    int    hitTest(const QPointF& pos, int* edge, Lane* lane) const;
    // The all-day pill rect for the r-th external event shown on day d.
    QRectF allDayPillRect(int day, int row) const;
    // Index into m_external of the read-only event under pos (band or timed),
    // or -1. Used by the right-click "adopt" path.
    int    hitTestExternal(const QPointF& pos) const;

    QDateTime dayTime(const QDate& day, double minutes) const;

    // Tooltip text for the hovered occurrence / external event ("" = none).
    QString hoverText(const QPointF& pos) const;

    Store&            m_store;
    QDate             m_weekStart;      // Monday
    Id                m_activeProject = -1;
    QList<Occurrence> m_occ;
    QHash<Id, QColor>  m_projectColors; // rebuilt on reload; drives block color
    QHash<Id, QString> m_projectNames;  // rebuilt on reload; drives tooltips

    // External read-only overlay (module 6), rebuilt on reload.
    QList<ExternalEvent> m_external;
    QList<QList<int>>    m_allDayByDay; // per weekday: indices into m_external
    double               m_allDayBandH = 0.0;

    // --- interaction ---------------------------------------------------------
    enum class Mode { None, Create, Move, ResizeTop, ResizeBottom };
    Mode      m_mode = Mode::None;
    bool      m_dragging = false;
    int       m_activeIdx = -1;         // index into m_occ for Move/Resize
    QDate     m_dragDay;                // current day column
    QDateTime m_dragStart, m_dragEnd;   // live planned span during a drag
    QDateTime m_origStart, m_origEnd;   // span at press, to detect a no-op click
    double    m_grabOffsetMin = 0;      // Move: grab-y minus block start
    double    m_createAnchorMin = 0;    // Create: fixed minute the drag started at
};

class CalendarView : public QWidget {
    Q_OBJECT
public:
    explicit CalendarView(Store& store, QWidget* parent = nullptr);

    void reload();
    int  occurrenceCount() const; // for tests
    double allDayBandHeight() const; // for tests
    // Re-align the viewed week to the (possibly changed) week-start setting.
    void applySettings();

public slots:
    void setActiveProject(zb::Id p);
    void goToday();
    void goPrev();
    void goNext();
    void reviewToday();
    // Save the visible week's planned blocks (incl. phantoms) as an .ics file.
    void exportWeek();

private:
    void updateLabel();

    Store&   m_store;
    CalGrid* m_grid;
    QLabel*  m_label;
};

} // namespace zb
