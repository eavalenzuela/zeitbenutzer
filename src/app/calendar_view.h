#pragma once

// Module 3: the calendar. A week time-grid (7 day columns x 24h) fit to the
// viewport. Renders planned blocks plus phantom occurrences expanded from
// recurring series; supports drag-to-create, drag-to-move, and edge-resize,
// snapping to 15 minutes. Editing a phantom occurrence materializes it first.
//
// CalGrid is the painted grid + interaction; CalendarView wraps it with a
// navigation toolbar (prev / today / next + week label).

#include <QDate>
#include <QDateTime>
#include <QGraphicsView>
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

signals:
    void weekChanged();

protected:
    void drawBackground(QPainter* p, const QRectF& rect) override;
    void resizeEvent(QResizeEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    // --- layout math (all in viewport == scene coordinates) -----------------
    static constexpr double kGutter = 54.0; // hour-label column
    static constexpr double kHeader = 34.0; // day-header band
    // Each day column splits into a planned lane (left) and an actual lane
    // (right) — the plan-vs-actual view.
    enum class Lane { Planned, Actual };

    double dayWidth() const;
    double laneWidth() const { return dayWidth() / 2.0; }
    double ppm() const;                 // pixels per minute
    double xForDay(int d) const;
    double xForLane(int d, Lane lane) const;
    double yForMinutes(double m) const;
    int    dayAt(double x) const;       // 0..6, or -1 in the gutter
    double minutesAt(double y) const;   // 0..1440 (unsnapped)
    double snap(double minutes) const;  // to the configured snap interval
    QRectF rectForSpan(const QDateTime& s, const QDateTime& e, Lane lane) const;
    // Topmost occurrence under pos. Fills edge (-1 top,0 body,1 bottom) and the
    // lane hit. Returns -1 on miss.
    int    hitTest(const QPointF& pos, int* edge, Lane* lane) const;

    QDateTime dayTime(const QDate& day, double minutes) const;

    Store&            m_store;
    QDate             m_weekStart;      // Monday
    Id                m_activeProject = -1;
    QList<Occurrence> m_occ;

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
    // Re-align the viewed week to the (possibly changed) week-start setting.
    void applySettings();

public slots:
    void setActiveProject(zb::Id p);
    void goToday();
    void goPrev();
    void goNext();

private:
    void updateLabel();

    Store&   m_store;
    CalGrid* m_grid;
    QLabel*  m_label;
};

} // namespace zb
