#include "app/calendar_view.h"

#include "app/block_dialog.h"
#include "app/block_edit_dialog.h"
#include "app/theme.h"
#include "storage/recurrence.h"
#include "storage/store.h"

#include <QGraphicsScene>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace zb {

namespace {
QDate mondayOf(const QDate& d)
{
    return d.addDays(-(d.dayOfWeek() - 1)); // dayOfWeek: Mon=1..Sun=7
}
double clampd(double v, double lo, double hi)
{
    return std::max(lo, std::min(hi, v));
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
    m_weekStart = mondayOf(QDate::currentDate());
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
    viewport()->update();
}

// --- layout -----------------------------------------------------------------

double CalGrid::dayWidth() const { return (viewport()->width() - kGutter) / 7.0; }
double CalGrid::ppm() const { return (viewport()->height() - kHeader) / (24.0 * 60.0); }
double CalGrid::xForDay(int d) const { return kGutter + d * dayWidth(); }
double CalGrid::xForLane(int d, Lane lane) const
{
    return xForDay(d) + (lane == Lane::Actual ? laneWidth() : 0.0);
}
double CalGrid::yForMinutes(double m) const { return kHeader + m * ppm(); }
int    CalGrid::dayAt(double x) const
{
    if (x < kGutter)
        return -1;
    return static_cast<int>(clampd((x - kGutter) / dayWidth(), 0, 6));
}
double CalGrid::minutesAt(double y) const
{
    return clampd((y - kHeader) / ppm(), 0, 1440);
}
double CalGrid::snap15(double minutes) { return std::round(minutes / 15.0) * 15.0; }

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
        p->drawLine(QPointF(x, kHeader), QPointF(x, h));
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

    // Blocks: planned lane (left) + actual lane (right) per occurrence.
    QFont blockFont = font();
    blockFont.setPointSize(9);
    p->setFont(blockFont);

    const QColor doneGreen(0x2e, 0x7d, 0x32);

    const auto drawBlock = [&](const QRectF& r, const QColor& brush,
                               const QPen& pen, const QString& label) {
        p->setBrush(brush);
        p->setPen(pen);
        p->drawRoundedRect(r, 4, 4);
        p->setPen(th.text);
        p->drawText(r.adjusted(4, 2, -3, -2),
                    Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, label);
    };

    for (int i = 0; i < m_occ.size(); ++i) {
        const Occurrence& o = m_occ.at(i);

        // --- planned lane ---
        if (!(m_dragging && m_activeIdx == i)) { // active one drawn live below
            const QRectF r = rectForSpan(o.plannedStart, o.plannedEnd, Lane::Planned);
            if (!r.isNull()) {
                QColor base = o.color.isEmpty() ? th.accent : QColor(o.color);
                if (!base.isValid())
                    base = th.accent;
                QString label = o.title.isEmpty() ? QStringLiteral("(block)") : o.title;
                if (o.seriesId)
                    label += QStringLiteral("  ↻");

                if (o.status == BlockStatus::Skipped) {
                    QPen pen(th.subtleText, 1.2);
                    pen.setStyle(Qt::DashLine);
                    drawBlock(r, QColor(th.subtleText.red(), th.subtleText.green(),
                                        th.subtleText.blue(), 30),
                              pen, label + QStringLiteral("  (skipped)"));
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
    if (e->button() != Qt::LeftButton || pos.x() < kGutter || pos.y() < kHeader) {
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
            m_createAnchorMin = snap15(minutesAt(pos.y()));
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
        QGraphicsView::mouseMoveEvent(e);
        return;
    }

    const double cur = minutesAt(pos.y());
    switch (m_mode) {
    case Mode::Create: {
        double s = std::min(m_createAnchorMin, snap15(cur));
        double en = std::max(m_createAnchorMin, snap15(cur));
        if (en - s < 15)
            en = s + 15;
        m_dragStart = dayTime(m_dragDay, s);
        m_dragEnd = dayTime(m_dragDay, en);
        break;
    }
    case Mode::Move: {
        const double dur = m_dragStart.secsTo(m_dragEnd) / 60.0;
        double s = snap15(cur - m_grabOffsetMin);
        s = clampd(s, 0, 1440 - dur);
        const int newDay = dayAt(pos.x());
        m_dragDay = m_weekStart.addDays(newDay);
        m_dragStart = dayTime(m_dragDay, s);
        m_dragEnd = m_dragStart.addSecs(static_cast<int>(dur * 60));
        break;
    }
    case Mode::ResizeTop: {
        const double endMin = m_dragEnd.time().msecsSinceStartOfDay() / 60000.0;
        double s = clampd(snap15(cur), 0, endMin - 15);
        m_dragStart = dayTime(m_dragDay, s);
        break;
    }
    case Mode::ResizeBottom: {
        const double startMin = m_dragStart.time().msecsSinceStartOfDay() / 60000.0;
        double en = clampd(snap15(cur), startMin + 15, 1440);
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
            // Reconcile: record (or clear) actual times per the chosen status.
            switch (dlg.status()) {
            case BlockStatus::Done:
                m_store.reconcileBlock(id, dlg.actualStart(), dlg.actualEnd(),
                                       BlockStatus::Done);
                break;
            case BlockStatus::Skipped:
                m_store.reconcileBlock(id, QDateTime(), QDateTime(),
                                       BlockStatus::Skipped);
                break;
            default: // Planned → un-reconcile (clear actuals)
                m_store.reconcileBlock(id, QDateTime(), QDateTime(),
                                       BlockStatus::Planned);
                break;
            }
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
    m_label = new QLabel(this);
    bar->addWidget(prev);
    bar->addWidget(today);
    bar->addWidget(next);
    bar->addSpacing(10);
    bar->addWidget(m_label, 1);
    layout->addLayout(bar);

    m_grid = new CalGrid(store, this);
    layout->addWidget(m_grid, 1);

    connect(prev, &QPushButton::clicked, this, &CalendarView::goPrev);
    connect(today, &QPushButton::clicked, this, &CalendarView::goToday);
    connect(next, &QPushButton::clicked, this, &CalendarView::goNext);
    connect(m_grid, &CalGrid::weekChanged, this, &CalendarView::updateLabel);

    reload();
}

void CalendarView::reload()
{
    m_grid->reload();
    updateLabel();
}

int CalendarView::occurrenceCount() const { return m_grid->occurrenceCount(); }

void CalendarView::setActiveProject(Id p) { m_grid->setActiveProject(p); }

void CalendarView::updateLabel()
{
    const QDate ws = m_grid->weekStart();
    const QDate we = ws.addDays(6);
    m_label->setText(QStringLiteral("%1 – %2")
                         .arg(ws.toString(QStringLiteral("ddd d MMM")),
                              we.toString(QStringLiteral("ddd d MMM yyyy"))));
}

void CalendarView::goToday()
{
    m_grid->setWeekStart(mondayOf(QDate::currentDate()));
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

} // namespace zb
