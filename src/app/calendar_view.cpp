#include "app/calendar_view.h"

#include "app/block_dialog.h"
#include "app/adopt_dialog.h"
#include "app/block_edit_dialog.h"
#include "app/day_review_dialog.h"
#include "app/reconcile_panel.h"
#include "app/settings.h"
#include "app/theme.h"
#include "storage/ical.h"
#include "storage/recurrence.h"
#include "storage/store.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QFile>
#include <QFileDialog>
#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace zb {

namespace {
double clampd(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
}

// Compact duration for the week-totals label: "3h30", "45m", "0m".
QString fmtDur(qint64 minutes)
{
    if (minutes <= 0)
        return QStringLiteral("0m");
    const qint64 h = minutes / 60, m = minutes % 60;
    if (h && m)
        return QStringLiteral("%1h%2").arg(h).arg(m, 2, 10, QLatin1Char('0'));
    if (h)
        return QStringLiteral("%1h").arg(h);
    return QStringLiteral("%1m").arg(m);
}

QString statusLabel(BlockStatus s)
{
    switch (s) {
    case BlockStatus::Done:    return QStringLiteral("done");
    case BlockStatus::Skipped: return QStringLiteral("skipped");
    case BlockStatus::Carried: return QStringLiteral("carried");
    default:                   return QStringLiteral("planned");
    }
}
} // namespace

// ===========================================================================
//  CalGrid
// ===========================================================================

CalGrid::CalGrid(Store& store, QWidget* parent)
    : QGraphicsView(parent), m_store(store)
{
    setScene(new QGraphicsScene(this));
    setFrameStyle(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setAlignment(Qt::AlignTop | Qt::AlignLeft);
    setMouseTracking(true);
    setFocusPolicy(Qt::ClickFocus); // so Esc (drag cancel) + shortcuts land here
    m_weekStart = weekStartFor(QDate::currentDate());

    // Keep the red now-line (and the today-column highlight across midnight)
    // moving without waiting for an interaction to force a repaint.
    auto* minuteTick = new QTimer(this);
    connect(minuteTick, &QTimer::timeout, this,
            [this] { viewport()->update(); });
    minuteTick->start(60 * 1000);
}

void CalGrid::setWeekStart(const QDate& monday)
{
    m_weekStart = monday;
    reload();
}

void CalGrid::reload()
{
    const QDateTime start(m_weekStart, QTime(0, 0));
    const QDateTime end(m_weekStart.addDays(6), QTime(23, 59, 59));
    m_occ = m_store.occurrencesInRange(start, end);

    m_projectColors.clear();
    m_projectNames.clear();
    for (const Project& p : m_store.listProjects(true)) {
        m_projectColors.insert(p.id, projectColor(p.color, p.id));
        m_projectNames.insert(p.id, p.name);
    }

    // External read-only overlay: split into per-day all-day buckets (band) and
    // timed events (drawn behind blocks in the planned lane). The band height
    // grows with the busiest day, so the time grid shifts down only as needed.
    m_external = m_store.externalEventsInRange(start, end);
    m_allDayByDay.assign(7, {});
    for (int i = 0; i < m_external.size(); ++i) {
        const ExternalEvent& e = m_external.at(i);
        if (!e.allDay)
            continue;
        // An all-day event can span several days; mark each in-view day.
        QDate d = e.start.date();
        const QDate last = e.end.addSecs(-1).date(); // DTEND is exclusive
        for (; d <= last; d = d.addDays(1)) {
            const int day = static_cast<int>(m_weekStart.daysTo(d));
            if (day >= 0 && day < 7)
                m_allDayByDay[day].append(i);
        }
    }
    int maxRows = 0;
    for (const auto& day : m_allDayByDay)
        maxRows = std::max<int>(maxRows, day.size());
    maxRows = std::min(maxRows, kMaxAllDayRows);
    m_allDayBandH = maxRows > 0 ? maxRows * kAllDayRowH + 2 * kAllDayPad : 0.0;

    viewport()->update();
}

// --- layout -----------------------------------------------------------------

double CalGrid::dayWidth() const { return (viewport()->width() - kGutter) / 7.0; }
double CalGrid::ppm() const { return (viewport()->height() - gridTop()) / (24.0 * 60.0); }
double CalGrid::xForDay(int d) const { return kGutter + d * dayWidth(); }
double CalGrid::xForLane(int d, Lane lane) const
{
    return xForDay(d) + (lane == Lane::Actual ? laneWidth() : 0.0);
}
double CalGrid::yForMinutes(double m) const { return gridTop() + m * ppm(); }
int    CalGrid::dayAt(double x) const
{
    if (x < kGutter)
        return -1;
    return static_cast<int>(clampd((x - kGutter) / dayWidth(), 0, 6));
}
double CalGrid::minutesAt(double y) const
{
    return clampd((y - gridTop()) / ppm(), 0, 1440);
}
double CalGrid::snap(double minutes) const { const double n = Settings::instance().snapMinutes(); return std::round(minutes / n) * n; }

QDateTime CalGrid::dayTime(const QDate& day, double minutes) const
{
    return QDateTime(day, QTime(0, 0).addSecs(static_cast<int>(minutes) * 60));
}

QRectF CalGrid::rectForSpan(const QDateTime& s, const QDateTime& e, Lane lane) const
{
    if (!s.isValid() || !e.isValid())
        return {};
    const int day = static_cast<int>(m_weekStart.daysTo(s.date()));
    if (day < 0 || day > 6)
        return {};
    const double startMin = s.time().msecsSinceStartOfDay() / 60000.0;
    double endMin = e.time().msecsSinceStartOfDay() / 60000.0;
    if (e.date() > s.date() || endMin <= startMin)
        endMin = 1440; // spills past midnight — clamp to end of day for now
    const double x = xForLane(day, lane) + 1.0;
    const double y = yForMinutes(startMin);
    const double h = std::max(14.0, yForMinutes(endMin) - y);
    return QRectF(x, y, laneWidth() - 2.0, h);
}

int CalGrid::hitTest(const QPointF& pos, int* edge, Lane* lane) const
{
    for (int i = m_occ.size() - 1; i >= 0; --i) {
        const Occurrence& o = m_occ.at(i);
        const QRectF rp = rectForSpan(o.plannedStart, o.plannedEnd, Lane::Planned);
        const QRectF ra = (o.actualStart && o.actualEnd)
                              ? rectForSpan(*o.actualStart, *o.actualEnd, Lane::Actual)
                              : QRectF();
        QRectF r;
        Lane hitLane = Lane::Planned;
        if (!rp.isNull() && rp.contains(pos)) {
            r = rp;
            hitLane = Lane::Planned;
        } else if (!ra.isNull() && ra.contains(pos)) {
            r = ra;
            hitLane = Lane::Actual;
        } else {
            continue;
        }
        if (lane)
            *lane = hitLane;
        if (edge) {
            if (pos.y() - r.top() <= 6.0)
                *edge = -1;
            else if (r.bottom() - pos.y() <= 6.0)
                *edge = 1;
            else
                *edge = 0;
        }
        return i;
    }
    return -1;
}

QRectF CalGrid::allDayPillRect(int day, int row) const
{
    const double x = xForDay(day) + 2.0;
    const double y = kHeader + kAllDayPad + row * kAllDayRowH;
    return QRectF(x, y, dayWidth() - 4.0, kAllDayRowH - 2.0);
}

int CalGrid::hitTestExternal(const QPointF& pos) const
{
    // All-day band first (it sits above the grid).
    if (pos.y() < gridTop()) {
        for (int day = 0; day < 7; ++day) {
            const QList<int>& evs = m_allDayByDay.at(day);
            const int shown = std::min<int>(evs.size(), kMaxAllDayRows);
            for (int row = 0; row < shown; ++row)
                if (allDayPillRect(day, row).contains(pos))
                    return evs.at(row);
        }
        return -1;
    }
    // Timed externals live in the planned lane; topmost (last drawn) wins.
    for (int i = m_external.size() - 1; i >= 0; --i) {
        const ExternalEvent& e = m_external.at(i);
        if (e.allDay)
            continue;
        const QRectF r = rectForSpan(e.start, e.end, Lane::Planned);
        if (!r.isNull() && r.contains(pos))
            return i;
    }
    return -1;
}

QString CalGrid::hoverText(const QPointF& pos) const
{
    int edge = 0;
    Lane lane = Lane::Planned;
    const int idx = hitTest(pos, &edge, &lane);
    if (idx >= 0) {
        const Occurrence& o = m_occ.at(idx);
        const QString span = QStringLiteral("%1–%2").arg(
            o.plannedStart.toString(QStringLiteral("HH:mm")),
            o.plannedEnd.toString(QStringLiteral("HH:mm")));
        QStringList lines;
        lines << (o.title.isEmpty() ? QStringLiteral("(block)") : o.title);
        lines << (o.projectId
                      ? m_projectNames.value(*o.projectId,
                                             QStringLiteral("(unknown project)"))
                      : QStringLiteral("(no project)"));
        QString when = QStringLiteral("plan ") + span;
        if (o.actualStart && o.actualEnd)
            when += QStringLiteral("  ·  actual %1–%2").arg(
                o.actualStart->toString(QStringLiteral("HH:mm")),
                o.actualEnd->toString(QStringLiteral("HH:mm")));
        lines << when;
        QString state = statusLabel(o.status);
        if (o.seriesId)
            state += QStringLiteral("  ·  repeats ↻");
        if (!o.materialized)
            state += QStringLiteral("  ·  not yet materialized");
        lines << state;
        return lines.join(QLatin1Char('\n'));
    }

    const int ext = hitTestExternal(pos);
    if (ext >= 0) {
        const ExternalEvent& e = m_external.at(ext);
        QStringList lines;
        lines << (e.summary.isEmpty() ? QStringLiteral("(event)") : e.summary);
        if (!e.location.isEmpty())
            lines << e.location;
        lines << (e.allDay
                      ? QStringLiteral("all day")
                      : QStringLiteral("%1–%2").arg(
                            e.start.toString(QStringLiteral("HH:mm")),
                            e.end.toString(QStringLiteral("HH:mm"))));
        lines << QStringLiteral("external calendar (read-only)");
        return lines.join(QLatin1Char('\n'));
    }
    return {};
}

// --- painting ---------------------------------------------------------------

void CalGrid::drawBackground(QPainter* p, const QRectF&)
{
    const Theme& th = currentTheme();
    const double w = viewport()->width();
    const double h = viewport()->height();
    p->setRenderHint(QPainter::Antialiasing, true);
    p->fillRect(QRectF(0, 0, w, h), th.panel);

    const QDate today = QDate::currentDate();

    // Today's column highlight.
    for (int d = 0; d < 7; ++d) {
        if (m_weekStart.addDays(d) == today) {
            p->fillRect(QRectF(xForDay(d), kHeader, dayWidth(), h - kHeader),
                        QColor(th.accent.red(), th.accent.green(), th.accent.blue(), 18));
        }
    }

    // Hour grid lines + gutter labels.
    p->setPen(QPen(th.border, 1));
    QFont small = font();
    small.setPointSize(9);
    p->setFont(small);
    for (int hr = 0; hr <= 24; ++hr) {
        const double y = yForMinutes(hr * 60);
        p->setPen(QPen(th.border, 1));
        p->drawLine(QPointF(kGutter, y), QPointF(w, y));
        if (hr < 24) {
            p->setPen(th.subtleText);
            p->drawText(QRectF(0, y, kGutter - 6, 16), Qt::AlignRight | Qt::AlignTop,
                        QStringLiteral("%1:00").arg(hr, 2, 10, QLatin1Char('0')));
        }
    }

    // Lane dividers (planned | actual) within each day.
    for (int d = 0; d < 7; ++d) {
        QPen pen(th.border, 1);
        pen.setStyle(Qt::DotLine);
        p->setPen(pen);
        const double x = xForLane(d, Lane::Actual);
        p->drawLine(QPointF(x, gridTop()), QPointF(x, h));
    }

    // Vertical day separators + header band.
    p->fillRect(QRectF(0, 0, w, kHeader), th.window);
    for (int d = 0; d <= 7; ++d) {
        const double x = xForDay(d);
        p->setPen(QPen(th.border, 1));
        p->drawLine(QPointF(x, 0), QPointF(x, h));
    }
    for (int d = 0; d < 7; ++d) {
        const QDate day = m_weekStart.addDays(d);
        const bool isToday = (day == today);
        QFont f = font();
        f.setPointSize(10);
        f.setBold(isToday);
        p->setFont(f);
        p->setPen(isToday ? th.accent : th.text);
        p->drawText(QRectF(xForDay(d), 1, dayWidth(), 19), Qt::AlignCenter,
                    day.toString(QStringLiteral("ddd d MMM")));
        // plan | actual sublabels.
        QFont sub = font();
        sub.setPointSize(8);
        p->setFont(sub);
        p->setPen(th.subtleText);
        p->drawText(QRectF(xForLane(d, Lane::Planned), 19, laneWidth(), 14),
                    Qt::AlignCenter, QStringLiteral("plan"));
        p->drawText(QRectF(xForLane(d, Lane::Actual), 19, laneWidth(), 14),
                    Qt::AlignCenter, QStringLiteral("actual"));
    }

    // All-day band: read-only external events as muted, source-colored pills,
    // capped per day with a "+N more" chip.
    if (m_allDayBandH > 0.0) {
        QFont bandFont = font();
        bandFont.setPointSize(8);
        p->setFont(bandFont);
        p->setPen(th.subtleText);
        p->drawText(QRectF(0, kHeader, kGutter - 6, kAllDayRowH),
                    Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("all-day"));
        for (int d = 0; d < 7; ++d) {
            const QList<int>& evs = m_allDayByDay.at(d);
            const int shown = std::min<int>(evs.size(), kMaxAllDayRows);
            for (int row = 0; row < shown; ++row) {
                const bool overflow =
                    (row == kMaxAllDayRows - 1 && evs.size() > kMaxAllDayRows);
                const ExternalEvent& e = m_external.at(evs.at(row));
                QColor c(e.sourceColor);
                if (!c.isValid())
                    c = th.subtleText;
                const QRectF r = allDayPillRect(d, row);
                p->setBrush(QColor(c.red(), c.green(), c.blue(), 50));
                p->setPen(QPen(c, 1.0));
                p->drawRoundedRect(r, 3, 3);
                p->setPen(th.text);
                const QString label =
                    overflow ? QStringLiteral("+%1 more")
                                   .arg(evs.size() - (kMaxAllDayRows - 1))
                             : (e.summary.isEmpty() ? QStringLiteral("(event)")
                                                    : e.summary);
                p->drawText(r.adjusted(4, 0, -3, 0),
                            Qt::AlignLeft | Qt::AlignVCenter, label);
            }
        }
    }

    // Blocks: planned lane (left) + actual lane (right) per occurrence.
    QFont blockFont = font();
    blockFont.setPointSize(9);
    p->setFont(blockFont);

    const QColor doneGreen(0x2e, 0x7d, 0x32);

    const auto drawBlock = [&](const QRectF& r, const QBrush& brush,
                               const QPen& pen, const QString& label) {
        p->setBrush(brush);
        p->setPen(pen);
        p->drawRoundedRect(r, 4, 4);
        p->setPen(th.text);
        p->drawText(r.adjusted(4, 2, -3, -2),
                    Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, label);
    };

    // Timed external events: faint striped fill behind the blocks, in the
    // planned lane — context to plan around, not commitments (no rollup/edit).
    for (const ExternalEvent& e : m_external) {
        if (e.allDay)
            continue;
        const QRectF r = rectForSpan(e.start, e.end, Lane::Planned);
        if (r.isNull())
            continue;
        QColor c(e.sourceColor);
        if (!c.isValid())
            c = th.subtleText;
        QPen pen(c, 1.0);
        pen.setStyle(Qt::DotLine);
        p->setBrush(QBrush(QColor(c.red(), c.green(), c.blue(), 22),
                           Qt::BDiagPattern));
        p->setPen(pen);
        p->drawRoundedRect(r, 4, 4);
        p->setPen(th.subtleText);
        p->drawText(r.adjusted(4, 2, -3, -2),
                    Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
                    e.summary.isEmpty() ? QStringLiteral("(event)") : e.summary);
    }

    for (int i = 0; i < m_occ.size(); ++i) {
        const Occurrence& o = m_occ.at(i);

        // --- planned lane ---
        if (!(m_dragging && m_activeIdx == i)) { // active one drawn live below
            const QRectF r = rectForSpan(o.plannedStart, o.plannedEnd, Lane::Planned);
            if (!r.isNull()) {
                // Block color follows its project; fall back to any block color,
                // else the theme accent.
                QColor base = th.accent;
                if (o.projectId && m_projectColors.contains(*o.projectId))
                    base = m_projectColors.value(*o.projectId);
                else if (!o.color.isEmpty() && QColor(o.color).isValid())
                    base = QColor(o.color);
                QString label = o.title.isEmpty() ? QStringLiteral("(block)") : o.title;
                if (o.seriesId)
                    label += QStringLiteral("  ↻");

                const bool orphan = !o.projectId.has_value();

                if (o.status == BlockStatus::Skipped) {
                    QPen pen(th.subtleText, 1.2);
                    pen.setStyle(Qt::DashLine);
                    drawBlock(r, QColor(th.subtleText.red(), th.subtleText.green(),
                                        th.subtleText.blue(), 30),
                              pen, label + QStringLiteral("  (skipped)"));
                } else if (orphan) {
                    // No project: hatched neutral grey + a ⚠ marker so unfiled
                    // blocks are visually obvious (they vanish from rollups).
                    QBrush hatch(QColor(th.subtleText.red(), th.subtleText.green(),
                                        th.subtleText.blue(), o.materialized ? 55 : 30),
                                 Qt::FDiagPattern);
                    QPen pen(th.subtleText, 1.2);
                    if (!o.materialized)
                        pen.setStyle(Qt::DashLine);
                    drawBlock(r, hatch, pen, QStringLiteral("⚠ ") + label);
                } else if (o.materialized) {
                    drawBlock(r, QColor(base.red(), base.green(), base.blue(), 60),
                              QPen(base, 1.5), label);
                } else {
                    QPen dashed(base, 1.2);
                    dashed.setStyle(Qt::DashLine);
                    drawBlock(r, QColor(base.red(), base.green(), base.blue(), 28),
                              dashed, label);
                }
            }
        }

        // --- actual lane (only when reconciled) ---
        if (o.actualStart && o.actualEnd) {
            const QRectF r = rectForSpan(*o.actualStart, *o.actualEnd, Lane::Actual);
            if (!r.isNull())
                drawBlock(r,
                          QColor(doneGreen.red(), doneGreen.green(),
                                 doneGreen.blue(), 70),
                          QPen(doneGreen, 1.5),
                          o.title.isEmpty() ? QStringLiteral("(done)") : o.title);
        }
    }

    // Live drag rect (planned lane).
    if (m_dragging) {
        const QRectF r = rectForSpan(m_dragStart, m_dragEnd, Lane::Planned);
        if (!r.isNull()) {
            p->setBrush(QColor(th.accent.red(), th.accent.green(), th.accent.blue(), 90));
            p->setPen(QPen(th.accent, 2));
            p->drawRoundedRect(r, 4, 4);
            const int mins = static_cast<int>(m_dragStart.secsTo(m_dragEnd) / 60);
            p->setPen(th.text);
            p->drawText(r.adjusted(5, 3, -4, -2), Qt::AlignLeft | Qt::AlignTop,
                        QStringLiteral("%1:%2 (%3m)")
                            .arg(m_dragStart.time().hour(), 2, 10, QLatin1Char('0'))
                            .arg(m_dragStart.time().minute(), 2, 10, QLatin1Char('0'))
                            .arg(mins));
        }
    }

    // Current-time line across today's column.
    for (int d = 0; d < 7; ++d) {
        if (m_weekStart.addDays(d) != today)
            continue;
        const double y =
            yForMinutes(QTime::currentTime().msecsSinceStartOfDay() / 60000.0);
        const QColor red(0xe5, 0x39, 0x35);
        p->setPen(QPen(red, 1.5));
        p->drawLine(QPointF(xForDay(d), y), QPointF(xForDay(d) + dayWidth(), y));
        p->setBrush(red);
        p->setPen(Qt::NoPen);
        p->drawEllipse(QPointF(xForDay(d), y), 3, 3);
    }
}

void CalGrid::resizeEvent(QResizeEvent* e)
{
    QGraphicsView::resizeEvent(e);
    scene()->setSceneRect(0, 0, viewport()->width(), viewport()->height());
    viewport()->update();
}

// --- interaction -------------------------------------------------------------

void CalGrid::mousePressEvent(QMouseEvent* e)
{
    const QPointF pos = mapToScene(e->pos());
    if (e->button() != Qt::LeftButton || pos.x() < kGutter || pos.y() < gridTop()) {
        QGraphicsView::mousePressEvent(e);
        return;
    }

    int edge = 0;
    Lane lane = Lane::Planned;
    const int idx = hitTest(pos, &edge, &lane);

    if (idx >= 0 && lane == Lane::Planned) {
        // Move/resize a planned block.
        m_activeIdx = idx;
        const Occurrence& o = m_occ.at(idx);
        m_dragStart = m_origStart = o.plannedStart;
        m_dragEnd = m_origEnd = o.plannedEnd;
        m_dragDay = o.plannedStart.date();
        if (edge == -1)
            m_mode = Mode::ResizeTop;
        else if (edge == 1)
            m_mode = Mode::ResizeBottom;
        else {
            m_mode = Mode::Move;
            const double startMin =
                o.plannedStart.time().msecsSinceStartOfDay() / 60000.0;
            m_grabOffsetMin = minutesAt(pos.y()) - startMin;
        }
        m_dragging = true;
    } else if (idx < 0) {
        // Empty space: create — but only in a day's planned (left) lane.
        const int day = dayAt(pos.x());
        if (pos.x() - xForDay(day) < laneWidth()) {
            m_mode = Mode::Create;
            m_activeIdx = -1;
            m_dragDay = m_weekStart.addDays(day);
            m_createAnchorMin = snap(minutesAt(pos.y()));
            m_dragStart = dayTime(m_dragDay, m_createAnchorMin);
            m_dragEnd = m_dragStart.addSecs(15 * 60);
            m_dragging = true;
        }
    }
    // (Clicks on an actual-lane block do nothing on press; double-click edits.)
    viewport()->update();
}

void CalGrid::mouseMoveEvent(QMouseEvent* e)
{
    const QPointF pos = mapToScene(e->pos());

    if (!m_dragging) {
        // Hover cursor: resize arrows near edges, move on a planned body.
        int edge = 0;
        Lane lane = Lane::Planned;
        const int idx = hitTest(pos, &edge, &lane);
        if (idx >= 0 && lane == Lane::Planned)
            setCursor(edge == 0 ? Qt::SizeAllCursor : Qt::SizeVerCursor);
        else
            setCursor(Qt::ArrowCursor);
        // Hover tooltip: what's under the pointer, at full detail.
        setToolTip(hoverText(pos));
        QGraphicsView::mouseMoveEvent(e);
        return;
    }

    const double cur = minutesAt(pos.y());
    switch (m_mode) {
    case Mode::Create: {
        double s = std::min(m_createAnchorMin, snap(cur));
        double en = std::max(m_createAnchorMin, snap(cur));
        if (en - s < 15)
            en = s + 15;
        m_dragStart = dayTime(m_dragDay, s);
        m_dragEnd = dayTime(m_dragDay, en);
        break;
    }
    case Mode::Move: {
        const double dur = m_dragStart.secsTo(m_dragEnd) / 60.0;
        double s = snap(cur - m_grabOffsetMin);
        s = clampd(s, 0, 1440 - dur);
        const int newDay = dayAt(pos.x());
        m_dragDay = m_weekStart.addDays(newDay);
        m_dragStart = dayTime(m_dragDay, s);
        m_dragEnd = m_dragStart.addSecs(static_cast<int>(dur * 60));
        break;
    }
    case Mode::ResizeTop: {
        const double endMin = m_dragEnd.time().msecsSinceStartOfDay() / 60000.0;
        double s = clampd(snap(cur), 0, endMin - 15);
        m_dragStart = dayTime(m_dragDay, s);
        break;
    }
    case Mode::ResizeBottom: {
        const double startMin = m_dragStart.time().msecsSinceStartOfDay() / 60000.0;
        double en = clampd(snap(cur), startMin + 15, 1440);
        m_dragEnd = dayTime(m_dragDay, en);
        break;
    }
    case Mode::None:
        break;
    }
    viewport()->update();
}

void CalGrid::mouseReleaseEvent(QMouseEvent* e)
{
    if (!m_dragging) {
        QGraphicsView::mouseReleaseEvent(e);
        return;
    }
    m_dragging = false;
    const Mode mode = m_mode;
    m_mode = Mode::None;

    if (mode == Mode::Create) {
        if (m_dragStart.secsTo(m_dragEnd) >= 15 * 60) {
            BlockDialog dlg(m_store, this);
            dlg.setInitial(QString(), m_activeProject);
            if (dlg.exec() == QDialog::Accepted) {
                // Build an RRULE from the repeat choice (anchor weekday matters).
                RRule rule;
                bool recurring = true;
                using R = BlockDialog::Repeat;
                switch (dlg.repeat()) {
                case R::None:     recurring = false; break;
                case R::Daily:    rule.freq = RRule::Freq::Daily; break;
                case R::Weekly:   rule.freq = RRule::Freq::Weekly; break;
                case R::Weekdays: rule.freq = RRule::Freq::Weekly;
                                  rule.byDay = {1, 2, 3, 4, 5}; break;
                case R::Monthly:  rule.freq = RRule::Freq::Monthly; break;
                }
                rule.count = dlg.count(); // 0 = unbounded

                if (!recurring) {
                    Block b;
                    b.title = dlg.title();
                    b.projectId = dlg.projectId();
                    b.plannedStart = m_dragStart;
                    b.plannedEnd = m_dragEnd;
                    b.status = BlockStatus::Planned;
                    m_store.createBlock(b);
                } else {
                    BlockSeries s;
                    s.title = dlg.title();
                    s.projectId = dlg.projectId();
                    s.rrule = rule.toString();
                    s.anchorStart = m_dragStart;
                    s.anchorEnd = m_dragEnd;
                    m_store.createBlockSeries(s);
                }
            }
        }
    } else if (m_activeIdx >= 0 && m_activeIdx < m_occ.size()
               && (m_dragStart != m_origStart || m_dragEnd != m_origEnd)) {
        // Only commit when the span actually changed — a plain click must not
        // materialize a phantom or rewrite the block.
        const Occurrence& o = m_occ.at(m_activeIdx);
        Id id = o.blockId;
        if (!o.materialized && o.seriesId) // editing a phantom → materialize it
            id = m_store.materializeBlockOccurrence(*o.seriesId, o.occurrenceDate);
        if (id > 0)
            m_store.updateBlockPlanned(id, m_dragStart, m_dragEnd);
    }

    m_activeIdx = -1;
    reload();
    emit weekChanged();
}

void CalGrid::mouseDoubleClickEvent(QMouseEvent* e)
{
    const QPointF pos = mapToScene(e->pos());
    int edge = 0;
    Lane lane = Lane::Planned;
    const int idx = hitTest(pos, &edge, &lane);
    if (idx < 0) {
        QGraphicsView::mouseDoubleClickEvent(e);
        return;
    }
    m_dragging = false; // cancel any drag the preceding press started
    m_mode = Mode::None;

    const Occurrence o = m_occ.at(idx);
    const bool series = o.seriesId.has_value();

    BlockEditDialog dlg(m_store, series, this);
    dlg.setInitial(o.title, o.projectId, o.plannedStart, o.plannedEnd, o.status,
                   o.actualStart, o.actualEnd);
    if (dlg.exec() != QDialog::Accepted)
        return;

    switch (dlg.action()) {
    case BlockEditDialog::Action::Save: {
        Id id = o.materialized
                    ? o.blockId
                    : (series ? m_store.materializeBlockOccurrence(
                                    *o.seriesId, o.occurrenceDate)
                              : -1);
        if (id > 0) {
            m_store.updateBlockMeta(id, dlg.title(), dlg.projectId());
            // Reconcile: record actuals / skip / carry per the chosen status.
            applyOutcome(m_store, id, dlg.status(), dlg.actualStart(),
                         dlg.actualEnd(), dlg.carryTarget());
        }
        break;
    }
    case BlockEditDialog::Action::DeleteOccurrence:
        if (series) {
            if (o.materialized)
                m_store.setBlockSkipped(o.blockId, true);
            else
                m_store.skipOccurrence(*o.seriesId, o.occurrenceDate);
        } else if (o.materialized) {
            m_store.deleteBlock(o.blockId);
        }
        break;
    case BlockEditDialog::Action::DeleteSeries:
        if (o.seriesId)
            m_store.deleteBlockSeries(*o.seriesId);
        break;
    }

    m_activeIdx = -1;
    reload();
    emit weekChanged();
}

void CalGrid::keyPressEvent(QKeyEvent* e)
{
    // Esc during a drag: abandon it — nothing was committed yet (create
    // commits on release; move/resize keeps the original span until then).
    if (e->key() == Qt::Key_Escape && m_dragging) {
        m_dragging = false;
        m_mode = Mode::None;
        m_activeIdx = -1;
        viewport()->update();
        return;
    }
    QGraphicsView::keyPressEvent(e);
}

void CalGrid::contextMenuEvent(QContextMenuEvent* e)
{
    const QPointF pos = mapToScene(mapFromGlobal(e->globalPos()));
    const int idx = hitTestExternal(pos);
    if (idx < 0) {
        QGraphicsView::contextMenuEvent(e);
        return;
    }
    const ExternalEvent ev = m_external.at(idx);

    QMenu menu(this);
    QAction* adopt = menu.addAction(QStringLiteral("Adopt into block…"));
    if (menu.exec(e->globalPos()) != adopt)
        return;

    AdoptDialog dlg(m_store, ev.summary, this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    Block b;
    b.title = dlg.title();
    b.projectId = dlg.projectId();
    b.plannedStart = ev.start;
    b.plannedEnd = ev.end;
    b.status = BlockStatus::Planned;
    const Id blockId = m_store.createBlock(b);
    m_store.markEventAdopted(ev.id, blockId);

    reload();
    emit weekChanged();
}

// ===========================================================================
//  CalendarView
// ===========================================================================

CalendarView::CalendarView(Store& store, QWidget* parent)
    : QWidget(parent), m_store(store)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* bar = new QHBoxLayout;
    auto* prev = new QPushButton(QStringLiteral("◀"), this);
    auto* today = new QPushButton(QStringLiteral("Today"), this);
    auto* next = new QPushButton(QStringLiteral("▶"), this);
    for (QPushButton* b : {prev, today, next})
        b->setMaximumWidth(b == today ? 80 : 36);
    prev->setToolTip(QStringLiteral("Previous week (Left)"));
    today->setToolTip(QStringLiteral("Jump to the current week (T)"));
    next->setToolTip(QStringLiteral("Next week (Right)"));
    auto* exportBtn = new QPushButton(QStringLiteral("Export…"), this);
    exportBtn->setToolTip(
        QStringLiteral("Save this week's planned blocks as an .ics file"));
    auto* review = new QPushButton(QStringLiteral("Review today"), this);
    m_label = new QLabel(this);
    bar->addWidget(prev);
    bar->addWidget(today);
    bar->addWidget(next);
    bar->addSpacing(10);
    bar->addWidget(m_label, 1);
    bar->addWidget(exportBtn);
    bar->addWidget(review);
    layout->addLayout(bar);

    m_grid = new CalGrid(store, this);
    layout->addWidget(m_grid, 1);

    connect(prev, &QPushButton::clicked, this, &CalendarView::goPrev);
    connect(today, &QPushButton::clicked, this, &CalendarView::goToday);
    connect(next, &QPushButton::clicked, this, &CalendarView::goNext);
    connect(exportBtn, &QPushButton::clicked, this, &CalendarView::exportWeek);
    connect(review, &QPushButton::clicked, this, &CalendarView::reviewToday);
    connect(m_grid, &CalGrid::weekChanged, this, &CalendarView::updateLabel);

    // Keyboard week navigation, scoped to the calendar so arrow keys keep
    // working normally in text fields elsewhere in the window.
    const auto shortcut = [this](const QKeySequence& seq, auto slot) {
        auto* s = new QShortcut(seq, this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, slot);
    };
    shortcut(QKeySequence(Qt::Key_Left), &CalendarView::goPrev);
    shortcut(QKeySequence(Qt::Key_Right), &CalendarView::goNext);
    shortcut(QKeySequence(Qt::Key_T), &CalendarView::goToday);
    shortcut(QKeySequence(Qt::Key_Home), &CalendarView::goToday);

    reload();
}

void CalendarView::reload()
{
    m_grid->reload();
    updateLabel();
}

int CalendarView::occurrenceCount() const { return m_grid->occurrenceCount(); }

double CalendarView::allDayBandHeight() const
{
    return m_grid->allDayBandHeight();
}

void CalendarView::applySettings()
{
    m_grid->setWeekStart(weekStartFor(m_grid->weekStart()));
    updateLabel();
}

void CalendarView::setActiveProject(Id p) { m_grid->setActiveProject(p); }

void CalendarView::updateLabel()
{
    const QDate ws = m_grid->weekStart();
    const QDate we = ws.addDays(6);

    // Week totals: planned vs done for everything visible (skipped occurrences
    // are already filtered out of occurrencesInRange) — the plan/reconcile
    // payoff at a glance, without opening the Time tab.
    const QDateTime winStart(ws, QTime(0, 0));
    const QDateTime winEnd(we, QTime(23, 59, 59));
    qint64 planMin = 0, doneMin = 0;
    for (const Occurrence& o : m_store.occurrencesInRange(winStart, winEnd)) {
        planMin += qMax<qint64>(0, o.plannedStart.secsTo(o.plannedEnd)) / 60;
        if (o.actualStart && o.actualEnd)
            doneMin += qMax<qint64>(0, o.actualStart->secsTo(*o.actualEnd)) / 60;
    }

    m_label->setText(QStringLiteral("%1 – %2   ·   plan %3 · done %4")
                         .arg(ws.toString(QStringLiteral("ddd d MMM")),
                              we.toString(QStringLiteral("ddd d MMM yyyy")),
                              fmtDur(planMin), fmtDur(doneMin)));
}

void CalendarView::goToday()
{
    m_grid->setWeekStart(weekStartFor(QDate::currentDate()));
    updateLabel();
}
void CalendarView::goPrev()
{
    m_grid->setWeekStart(m_grid->weekStart().addDays(-7));
    updateLabel();
}
void CalendarView::goNext()
{
    m_grid->setWeekStart(m_grid->weekStart().addDays(7));
    updateLabel();
}

void CalendarView::exportWeek()
{
    const QDate ws = m_grid->weekStart();
    const QDateTime winStart(ws, QTime(0, 0));
    const QDateTime winEnd(ws.addDays(6), QTime(23, 59, 59));

    // Everything planned this week — persisted blocks and phantom occurrences
    // alike (both are the plan). Actuals stay local; the export is the schedule.
    QList<ICalEvent> events;
    for (const Occurrence& o : m_store.occurrencesInRange(winStart, winEnd)) {
        ICalEvent e;
        e.uid = o.materialized
                    ? QStringLiteral("zb-block-%1@zeitbenutzer").arg(o.blockId)
                    : QStringLiteral("zb-series-%1-%2@zeitbenutzer")
                          .arg(o.seriesId ? *o.seriesId : 0)
                          .arg(o.occurrenceDate.toString(QStringLiteral("yyyyMMdd")));
        e.summary = o.title.isEmpty() ? QStringLiteral("(block)") : o.title;
        e.start = o.plannedStart;
        e.end = o.plannedEnd;
        events.append(e);
    }
    if (events.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Export week"),
                                 QStringLiteral("Nothing planned this week."));
        return;
    }

    const QString suggested = QStringLiteral("zeitbenutzer-week-%1.ics")
                                  .arg(ws.toString(QStringLiteral("yyyy-MM-dd")));
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export week as iCalendar"), suggested,
        QStringLiteral("iCalendar (*.ics)"));
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::warning(this, QStringLiteral("Export week"),
                             QStringLiteral("Could not write %1:\n%2")
                                 .arg(path, f.errorString()));
        return;
    }
    f.write(writeICalendar(events));
    f.close();
}

void CalendarView::reviewToday()
{
    const QDate today = QDate::currentDate();
    const QDateTime dayStart(today, QTime(0, 0));
    const QDateTime dayEnd(today, QTime(23, 59, 59));

    QList<Occurrence> queue;
    for (const Occurrence& o : m_store.occurrencesInRange(dayStart, dayEnd)) {
        if (o.status == BlockStatus::Planned && !o.skipped)
            queue.append(o);
    }
    std::sort(queue.begin(), queue.end(),
              [](const Occurrence& a, const Occurrence& b) {
                  return a.plannedStart < b.plannedStart;
              });

    if (queue.isEmpty()) {
        QMessageBox::information(
            this, QStringLiteral("Review today"),
            QStringLiteral("Nothing to review — today's all reconciled."));
        return;
    }

    DayReviewDialog dlg(m_store, queue, this);
    dlg.exec();
    reload();
}

} // namespace zb
