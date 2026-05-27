#include "storage/store.h"

#include "storage/recurrence.h"

#include <QHash>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTimeZone>
#include <algorithm>

namespace zb {

namespace {

// ---- conversion helpers between struct fields and SQLite columns -----------

QVariant toEpoch(const QDateTime& dt)
{
    return dt.isValid() ? QVariant(dt.toUTC().toSecsSinceEpoch()) : QVariant();
}
QVariant toEpoch(const std::optional<QDateTime>& dt)
{
    return (dt && dt->isValid()) ? QVariant(dt->toUTC().toSecsSinceEpoch())
                                 : QVariant();
}
QDateTime fromEpoch(const QVariant& v)
{
    return v.isNull() ? QDateTime()
                      : QDateTime::fromSecsSinceEpoch(v.toLongLong(), QTimeZone::LocalTime);
}
std::optional<QDateTime> optFromEpoch(const QVariant& v)
{
    if (v.isNull())
        return std::nullopt;
    return QDateTime::fromSecsSinceEpoch(v.toLongLong(), QTimeZone::LocalTime);
}
QVariant fromOptId(const std::optional<Id>& id)
{
    return id ? QVariant(*id) : QVariant();
}
std::optional<Id> toOptId(const QVariant& v)
{
    if (v.isNull())
        return std::nullopt;
    return v.toLongLong();
}
QVariant dateText(const QDate& d)
{
    return d.isValid() ? QVariant(d.toString(Qt::ISODate)) : QVariant();
}
// Coerce a possibly-null QString to "" so binding never violates a NOT NULL
// text column (SQLite's DEFAULT applies only to omitted columns, not bound NULLs).
QString txt(const QString& s)
{
    return s.isNull() ? QString::fromLatin1("") : s;
}
QDate dateFrom(const QVariant& v)
{
    return v.isNull() ? QDate() : QDate::fromString(v.toString(), Qt::ISODate);
}

// Column order shared by every block SELECT below.
const char* const kBlockCols =
    "id, series_id, occurrence_date, planned_start, planned_end, "
    "actual_start, actual_end, project_id, title, color, status, "
    "detached, skipped, carried_from";

Block readBlock(const QSqlQuery& q)
{
    Block b;
    b.id             = q.value(0).toLongLong();
    b.seriesId       = toOptId(q.value(1));
    b.occurrenceDate = dateFrom(q.value(2));
    b.plannedStart   = fromEpoch(q.value(3));
    b.plannedEnd     = fromEpoch(q.value(4));
    b.actualStart    = optFromEpoch(q.value(5));
    b.actualEnd      = optFromEpoch(q.value(6));
    b.projectId      = toOptId(q.value(7));
    b.title          = q.value(8).toString();
    b.color          = q.value(9).toString();
    b.status         = static_cast<BlockStatus>(q.value(10).toInt());
    b.detached       = q.value(11).toInt() != 0;
    b.skipped        = q.value(12).toInt() != 0;
    b.carriedFrom    = toOptId(q.value(13));
    return b;
}

QString seriesDateKey(Id seriesId, const QDate& d)
{
    return QStringLiteral("%1|%2").arg(seriesId).arg(d.toString(Qt::ISODate));
}

} // namespace

// ---- projects --------------------------------------------------------------

Id Store::createProject(const Project& p)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "INSERT INTO project(parent_id, name, color, archived, sort) "
        "VALUES(?,?,?,?,?)"));
    q.addBindValue(fromOptId(p.parentId));
    q.addBindValue(txt(p.name));
    q.addBindValue(txt(p.color));
    q.addBindValue(p.archived ? 1 : 0);
    q.addBindValue(p.sort);
    q.exec();
    return q.lastInsertId().toLongLong();
}

QList<Project> Store::listProjects(bool includeArchived)
{
    QList<Project> out;
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "SELECT id, parent_id, name, color, archived, sort FROM project "
        "WHERE (? = 1 OR archived = 0) ORDER BY sort, id"));
    q.addBindValue(includeArchived ? 1 : 0);
    q.exec();
    while (q.next()) {
        Project p;
        p.id       = q.value(0).toLongLong();
        p.parentId = toOptId(q.value(1));
        p.name     = q.value(2).toString();
        p.color    = q.value(3).toString();
        p.archived = q.value(4).toInt() != 0;
        p.sort     = q.value(5).toInt();
        out.append(p);
    }
    return out;
}

QList<Id> Store::projectAndDescendants(Id rootId)
{
    // Load the whole parent map once, then BFS — cheap for realistic tree sizes.
    QHash<Id, QList<Id>> children;
    QSqlQuery q(m_db.handle());
    q.exec(QStringLiteral("SELECT id, parent_id FROM project"));
    while (q.next()) {
        const auto parent = toOptId(q.value(1));
        if (parent)
            children[*parent].append(q.value(0).toLongLong());
    }

    QList<Id> out{rootId};
    for (int i = 0; i < out.size(); ++i)
        out.append(children.value(out.at(i)));
    return out;
}

void Store::renameProject(Id projectId, const QString& name)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral("UPDATE project SET name = ? WHERE id = ?"));
    q.addBindValue(txt(name));
    q.addBindValue(projectId);
    q.exec();
}

void Store::setProjectColor(Id projectId, const QString& hex)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral("UPDATE project SET color = ? WHERE id = ?"));
    q.addBindValue(txt(hex));
    q.addBindValue(projectId);
    q.exec();
}

void Store::deleteProject(Id projectId)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral("DELETE FROM project WHERE id = ?"));
    q.addBindValue(projectId);
    q.exec();
}

// ---- notes -----------------------------------------------------------------

Id Store::createNote(const Note& n)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "INSERT INTO note(project_id, title, body_md, created_at, updated_at) "
        "VALUES(?,?,?,?,?)"));
    q.addBindValue(n.projectId);
    q.addBindValue(txt(n.title));
    q.addBindValue(txt(n.bodyMd));
    q.addBindValue(toEpoch(n.createdAt.isValid() ? n.createdAt
                                                 : QDateTime::currentDateTimeUtc()));
    q.addBindValue(toEpoch(n.updatedAt.isValid() ? n.updatedAt
                                                 : QDateTime::currentDateTimeUtc()));
    q.exec();
    return q.lastInsertId().toLongLong();
}

QList<Note> Store::notesForProject(Id projectId)
{
    QList<Note> out;
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "SELECT id, project_id, title, body_md, created_at, updated_at "
        "FROM note WHERE project_id = ? ORDER BY updated_at DESC"));
    q.addBindValue(projectId);
    q.exec();
    while (q.next()) {
        Note n;
        n.id        = q.value(0).toLongLong();
        n.projectId = q.value(1).toLongLong();
        n.title     = q.value(2).toString();
        n.bodyMd    = q.value(3).toString();
        n.createdAt = fromEpoch(q.value(4));
        n.updatedAt = fromEpoch(q.value(5));
        out.append(n);
    }
    return out;
}

Note Store::note(Id noteId)
{
    Note n;
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "SELECT id, project_id, title, body_md, created_at, updated_at "
        "FROM note WHERE id = ?"));
    q.addBindValue(noteId);
    q.exec();
    if (q.next()) {
        n.id        = q.value(0).toLongLong();
        n.projectId = q.value(1).toLongLong();
        n.title     = q.value(2).toString();
        n.bodyMd    = q.value(3).toString();
        n.createdAt = fromEpoch(q.value(4));
        n.updatedAt = fromEpoch(q.value(5));
    }
    return n;
}

void Store::updateNote(Id noteId, const QString& title, const QString& body)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "UPDATE note SET title = ?, body_md = ?, updated_at = ? WHERE id = ?"));
    q.addBindValue(txt(title));
    q.addBindValue(txt(body));
    q.addBindValue(toEpoch(QDateTime::currentDateTimeUtc()));
    q.addBindValue(noteId);
    q.exec();
}

void Store::deleteNote(Id noteId)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral("DELETE FROM note WHERE id = ?"));
    q.addBindValue(noteId);
    q.exec();
}

// ---- tasks ------------------------------------------------------------------

Id Store::createTask(const Task& t)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "INSERT INTO task(project_id, series_id, occurrence_date, title, "
        "status, estimate_min, due, completed_at, sort) "
        "VALUES(?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(t.projectId);
    q.addBindValue(fromOptId(t.seriesId));
    q.addBindValue(dateText(t.occurrenceDate));
    q.addBindValue(txt(t.title));
    q.addBindValue(static_cast<int>(t.status));
    q.addBindValue(t.estimateMin);
    q.addBindValue(toEpoch(t.due));
    q.addBindValue(toEpoch(t.completedAt));
    q.addBindValue(t.sort);
    q.exec();
    return q.lastInsertId().toLongLong();
}

void Store::setTaskStatus(Id taskId, TaskStatus status, const QDateTime& completedAt)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "UPDATE task SET status = ?, completed_at = ? WHERE id = ?"));
    q.addBindValue(static_cast<int>(status));
    if (status == TaskStatus::Done)
        q.addBindValue(toEpoch(completedAt.isValid() ? completedAt
                                                     : QDateTime::currentDateTimeUtc()));
    else
        q.addBindValue(QVariant());
    q.addBindValue(taskId);
    q.exec();
}

void Store::updateTask(Id taskId, const QString& title, int estimateMin)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "UPDATE task SET title = ?, estimate_min = ? WHERE id = ?"));
    q.addBindValue(txt(title));
    q.addBindValue(estimateMin);
    q.addBindValue(taskId);
    q.exec();
}

void Store::deleteTask(Id taskId)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral("DELETE FROM task WHERE id = ?"));
    q.addBindValue(taskId);
    q.exec();
}

QList<Task> Store::tasksForProject(Id projectId)
{
    QList<Task> out;
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "SELECT id, project_id, series_id, occurrence_date, title, status, "
        "estimate_min, due, completed_at, sort FROM task "
        "WHERE project_id = ? ORDER BY sort, id"));
    q.addBindValue(projectId);
    q.exec();
    while (q.next()) {
        Task t;
        t.id             = q.value(0).toLongLong();
        t.projectId      = q.value(1).toLongLong();
        t.seriesId       = toOptId(q.value(2));
        t.occurrenceDate = dateFrom(q.value(3));
        t.title          = q.value(4).toString();
        t.status         = static_cast<TaskStatus>(q.value(5).toInt());
        t.estimateMin    = q.value(6).toInt();
        t.due            = optFromEpoch(q.value(7));
        t.completedAt    = optFromEpoch(q.value(8));
        t.sort           = q.value(9).toInt();
        out.append(t);
    }
    return out;
}

// ---- series -----------------------------------------------------------------

Id Store::createBlockSeries(const BlockSeries& s)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "INSERT INTO block_series(project_id, title, color, rrule, "
        "anchor_start, anchor_end, until) VALUES(?,?,?,?,?,?,?)"));
    q.addBindValue(fromOptId(s.projectId));
    q.addBindValue(txt(s.title));
    q.addBindValue(txt(s.color));
    q.addBindValue(txt(s.rrule));
    q.addBindValue(toEpoch(s.anchorStart));
    q.addBindValue(toEpoch(s.anchorEnd));
    q.addBindValue(toEpoch(s.until));
    q.exec();
    return q.lastInsertId().toLongLong();
}

Id Store::createTaskSeries(const TaskSeries& s)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "INSERT INTO task_series(project_id, title, estimate_min, rrule, "
        "anchor_due, until) VALUES(?,?,?,?,?,?)"));
    q.addBindValue(s.projectId);
    q.addBindValue(txt(s.title));
    q.addBindValue(s.estimateMin);
    q.addBindValue(txt(s.rrule));
    q.addBindValue(toEpoch(s.anchorDue));
    q.addBindValue(toEpoch(s.until));
    q.exec();
    return q.lastInsertId().toLongLong();
}

// ---- blocks -----------------------------------------------------------------

Id Store::createBlock(const Block& b)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "INSERT INTO block(series_id, occurrence_date, planned_start, "
        "planned_end, actual_start, actual_end, project_id, title, color, "
        "status, detached, skipped, carried_from) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(fromOptId(b.seriesId));
    q.addBindValue(dateText(b.occurrenceDate));
    q.addBindValue(toEpoch(b.plannedStart));
    q.addBindValue(toEpoch(b.plannedEnd));
    q.addBindValue(toEpoch(b.actualStart));
    q.addBindValue(toEpoch(b.actualEnd));
    q.addBindValue(fromOptId(b.projectId));
    q.addBindValue(txt(b.title));
    q.addBindValue(txt(b.color));
    q.addBindValue(static_cast<int>(b.status));
    q.addBindValue(b.detached ? 1 : 0);
    q.addBindValue(b.skipped ? 1 : 0);
    q.addBindValue(fromOptId(b.carriedFrom));
    q.exec();
    return q.lastInsertId().toLongLong();
}

QList<Block> Store::blocksInRange(const QDateTime& winStart, const QDateTime& winEnd)
{
    QList<Block> out;
    QSqlQuery q(m_db.handle());
    q.prepare(QString::fromLatin1(
                  "SELECT %1 FROM block WHERE planned_start BETWEEN ? AND ? "
                  "ORDER BY planned_start")
                  .arg(QLatin1String(kBlockCols)));
    q.addBindValue(toEpoch(winStart));
    q.addBindValue(toEpoch(winEnd));
    q.exec();
    while (q.next())
        out.append(readBlock(q));
    return out;
}

void Store::linkBlockTask(Id blockId, Id taskId)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO block_task(block_id, task_id) VALUES(?,?)"));
    q.addBindValue(blockId);
    q.addBindValue(taskId);
    q.exec();
}

void Store::linkBlockNote(Id blockId, Id noteId)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO block_note(block_id, note_id) VALUES(?,?)"));
    q.addBindValue(blockId);
    q.addBindValue(noteId);
    q.exec();
}

void Store::updateBlockPlanned(Id blockId, const QDateTime& start,
                               const QDateTime& end)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "UPDATE block SET planned_start = ?, planned_end = ? WHERE id = ?"));
    q.addBindValue(toEpoch(start));
    q.addBindValue(toEpoch(end));
    q.addBindValue(blockId);
    q.exec();
}

void Store::updateBlockMeta(Id blockId, const QString& title,
                            std::optional<Id> projectId)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "UPDATE block SET title = ?, project_id = ? WHERE id = ?"));
    q.addBindValue(txt(title));
    q.addBindValue(fromOptId(projectId));
    q.addBindValue(blockId);
    q.exec();
}

void Store::deleteBlock(Id blockId)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral("DELETE FROM block WHERE id = ?"));
    q.addBindValue(blockId);
    q.exec();
}

void Store::deleteBlockSeries(Id seriesId)
{
    QSqlQuery del(m_db.handle());
    del.prepare(QStringLiteral("DELETE FROM block WHERE series_id = ?"));
    del.addBindValue(seriesId);
    del.exec();

    QSqlQuery ds(m_db.handle());
    ds.prepare(QStringLiteral("DELETE FROM block_series WHERE id = ?"));
    ds.addBindValue(seriesId);
    ds.exec();
}

void Store::setBlockSkipped(Id blockId, bool skipped)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "UPDATE block SET skipped = ?, status = ? WHERE id = ?"));
    q.addBindValue(skipped ? 1 : 0);
    q.addBindValue(static_cast<int>(skipped ? BlockStatus::Skipped
                                            : BlockStatus::Planned));
    q.addBindValue(blockId);
    q.exec();
}

void Store::skipOccurrence(Id seriesId, const QDate& date)
{
    const Id id = materializeBlockOccurrence(seriesId, date);
    if (id > 0)
        setBlockSkipped(id, true);
}

Id Store::materializeBlockOccurrence(Id seriesId, const QDate& date)
{
    // Already materialized? Return the existing block.
    {
        QSqlQuery q(m_db.handle());
        q.prepare(QStringLiteral(
            "SELECT id FROM block WHERE series_id = ? AND occurrence_date = ?"));
        q.addBindValue(seriesId);
        q.addBindValue(dateText(date));
        q.exec();
        if (q.next())
            return q.value(0).toLongLong();
    }

    // Load the series template.
    QSqlQuery s(m_db.handle());
    s.prepare(QStringLiteral(
        "SELECT project_id, title, color, anchor_start, anchor_end "
        "FROM block_series WHERE id = ?"));
    s.addBindValue(seriesId);
    s.exec();
    if (!s.next())
        return -1;

    const auto projectId = toOptId(s.value(0));
    const QString title  = s.value(1).toString();
    const QString color  = s.value(2).toString();
    const QDateTime aStart = fromEpoch(s.value(3));
    const QDateTime aEnd   = fromEpoch(s.value(4));
    const qint64 durationSecs = aStart.secsTo(aEnd);

    Block b;
    b.seriesId       = seriesId;
    b.occurrenceDate = date;
    b.plannedStart   = QDateTime(date, aStart.time());
    b.plannedEnd     = b.plannedStart.addSecs(durationSecs);
    b.projectId      = projectId;
    b.title          = title;
    b.color          = color;
    b.status         = BlockStatus::Planned;
    return createBlock(b);
}

void Store::reconcileBlock(Id blockId, const QDateTime& actualStart,
                           const QDateTime& actualEnd, BlockStatus status)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "UPDATE block SET actual_start = ?, actual_end = ?, status = ? "
        "WHERE id = ?"));
    q.addBindValue(toEpoch(actualStart));
    q.addBindValue(toEpoch(actualEnd));
    q.addBindValue(static_cast<int>(status));
    q.addBindValue(blockId);
    q.exec();
}

Id Store::carryOverBlock(Id blockId, const QDate& toDate)
{
    // Read the original.
    QSqlQuery q(m_db.handle());
    q.prepare(QString::fromLatin1("SELECT %1 FROM block WHERE id = ?")
                  .arg(QLatin1String(kBlockCols)));
    q.addBindValue(blockId);
    q.exec();
    if (!q.next())
        return -1;
    const Block orig = readBlock(q);

    // Clone onto the new day, same time-of-day, unreconciled.
    const qint64 durationSecs = orig.plannedStart.secsTo(orig.plannedEnd);
    Block clone;
    clone.plannedStart = QDateTime(toDate, orig.plannedStart.time());
    clone.plannedEnd   = clone.plannedStart.addSecs(durationSecs);
    clone.projectId    = orig.projectId;
    clone.title        = orig.title;
    clone.color        = orig.color;
    clone.status       = BlockStatus::Planned;
    clone.carriedFrom  = orig.id;
    const Id newId = createBlock(clone);

    // Copy task/note links to the clone.
    QSqlQuery ct(m_db.handle());
    ct.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO block_task(block_id, task_id) "
        "SELECT ?, task_id FROM block_task WHERE block_id = ?"));
    ct.addBindValue(newId);
    ct.addBindValue(blockId);
    ct.exec();

    QSqlQuery cn(m_db.handle());
    cn.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO block_note(block_id, note_id) "
        "SELECT ?, note_id FROM block_note WHERE block_id = ?"));
    cn.addBindValue(newId);
    cn.addBindValue(blockId);
    cn.exec();

    // Mark the original as carried (it stays on its own day).
    QSqlQuery mk(m_db.handle());
    mk.prepare(QStringLiteral("UPDATE block SET status = ? WHERE id = ?"));
    mk.addBindValue(static_cast<int>(BlockStatus::Carried));
    mk.addBindValue(blockId);
    mk.exec();

    return newId;
}

// ---- calendar view ----------------------------------------------------------

QList<Occurrence> Store::occurrencesInRange(const QDateTime& winStart,
                                            const QDateTime& winEnd)
{
    QList<Occurrence> out;
    QSet<QString> materialized; // seriesDateKey for occurrences already persisted

    // 1. Persisted blocks (one-offs and materialized occurrences).
    for (const Block& b : blocksInRange(winStart, winEnd)) {
        // A skipped occurrence is hidden, but still suppresses its phantom so
        // the deleted instance doesn't reappear from the series.
        if (b.skipped) {
            if (b.seriesId && b.occurrenceDate.isValid())
                materialized.insert(seriesDateKey(*b.seriesId, b.occurrenceDate));
            continue;
        }
        Occurrence o;
        o.materialized   = true;
        o.blockId        = b.id;
        o.seriesId       = b.seriesId;
        o.projectId      = b.projectId;
        o.occurrenceDate = b.occurrenceDate.isValid() ? b.occurrenceDate
                                                      : b.plannedStart.date();
        o.plannedStart   = b.plannedStart;
        o.plannedEnd     = b.plannedEnd;
        o.actualStart    = b.actualStart;
        o.actualEnd      = b.actualEnd;
        o.title          = b.title;
        o.color          = b.color;
        o.status         = b.status;
        o.skipped        = b.skipped;
        out.append(o);
        if (b.seriesId && b.occurrenceDate.isValid())
            materialized.insert(seriesDateKey(*b.seriesId, b.occurrenceDate));
    }

    // 2. Phantom occurrences expanded from every series, minus the ones already
    //    materialized for the same (series, date).
    QSqlQuery s(m_db.handle());
    s.exec(QStringLiteral(
        "SELECT id, project_id, title, color, rrule, anchor_start, anchor_end, "
        "until FROM block_series"));
    while (s.next()) {
        const Id seriesId        = s.value(0).toLongLong();
        const auto projectId     = toOptId(s.value(1));
        const QString title      = s.value(2).toString();
        const QString color      = s.value(3).toString();
        const auto rule          = RRule::parse(s.value(4).toString());
        const QDateTime aStart   = fromEpoch(s.value(5));
        const QDateTime aEnd     = fromEpoch(s.value(6));
        const QDateTime until    = fromEpoch(s.value(7));
        if (!rule)
            continue;
        const qint64 durationSecs = aStart.secsTo(aEnd);

        for (const QDateTime& dt : rule->expand(aStart, until, winStart, winEnd)) {
            const QDate d = dt.date();
            if (materialized.contains(seriesDateKey(seriesId, d)))
                continue;
            Occurrence o;
            o.materialized   = false;
            o.seriesId       = seriesId;
            o.projectId      = projectId;
            o.occurrenceDate = d;
            o.plannedStart   = dt;
            o.plannedEnd     = dt.addSecs(durationSecs);
            o.title          = title;
            o.color          = color;
            o.status         = BlockStatus::Planned;
            out.append(o);
        }
    }

    std::sort(out.begin(), out.end(), [](const Occurrence& a, const Occurrence& b) {
        return a.plannedStart < b.plannedStart;
    });
    return out;
}

// ---- external calendars -----------------------------------------------------

namespace {

ExternalSource readSource(const QSqlQuery& q)
{
    ExternalSource s;
    s.id        = q.value(0).toLongLong();
    s.kind      = static_cast<ExternalSource::Kind>(q.value(1).toInt());
    s.location  = q.value(2).toString();
    s.name      = q.value(3).toString();
    s.color     = q.value(4).toString();
    s.enabled   = q.value(5).toInt() != 0;
    s.lastSynced = optFromEpoch(q.value(6));
    return s;
}

} // namespace

Id Store::createExternalSource(const ExternalSource& s)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "INSERT INTO external_source(kind, location, name, color, enabled) "
        "VALUES(?,?,?,?,?)"));
    q.addBindValue(static_cast<int>(s.kind));
    q.addBindValue(txt(s.location));
    q.addBindValue(txt(s.name));
    q.addBindValue(txt(s.color));
    q.addBindValue(s.enabled ? 1 : 0);
    q.exec();
    return q.lastInsertId().toLongLong();
}

QList<ExternalSource> Store::listExternalSources()
{
    QList<ExternalSource> out;
    QSqlQuery q(m_db.handle());
    q.exec(QStringLiteral(
        "SELECT id, kind, location, name, color, enabled, last_synced "
        "FROM external_source ORDER BY id"));
    while (q.next())
        out.append(readSource(q));
    return out;
}

void Store::updateExternalSource(const ExternalSource& s)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "UPDATE external_source SET kind=?, location=?, name=?, color=?, "
        "enabled=? WHERE id=?"));
    q.addBindValue(static_cast<int>(s.kind));
    q.addBindValue(txt(s.location));
    q.addBindValue(txt(s.name));
    q.addBindValue(txt(s.color));
    q.addBindValue(s.enabled ? 1 : 0);
    q.addBindValue(s.id);
    q.exec();
}

void Store::deleteExternalSource(Id sourceId)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral("DELETE FROM external_source WHERE id = ?"));
    q.addBindValue(sourceId);
    q.exec();
}

void Store::setSourceSynced(Id sourceId, const QDateTime& when)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "UPDATE external_source SET last_synced = ? WHERE id = ?"));
    q.addBindValue(toEpoch(when));
    q.addBindValue(sourceId);
    q.exec();
}

void Store::replaceSourceEvents(Id sourceId, const QList<ExternalEvent>& events)
{
    QSqlDatabase db = m_db.handle();
    db.transaction();

    // Remember which uids were adopted so re-sync doesn't lose the link.
    QHash<QString, Id> adopted;
    {
        QSqlQuery q(db);
        q.prepare(QStringLiteral(
            "SELECT uid, adopted_block_id FROM external_event "
            "WHERE source_id = ? AND adopted_block_id IS NOT NULL"));
        q.addBindValue(sourceId);
        q.exec();
        while (q.next())
            adopted.insert(q.value(0).toString(), q.value(1).toLongLong());
    }

    QSqlQuery del(db);
    del.prepare(QStringLiteral("DELETE FROM external_event WHERE source_id = ?"));
    del.addBindValue(sourceId);
    del.exec();

    QSqlQuery ins(db);
    ins.prepare(QStringLiteral(
        "INSERT INTO external_event(source_id, uid, summary, location_txt, "
        "start_utc, end_utc, all_day, adopted_block_id) VALUES(?,?,?,?,?,?,?,?)"));
    for (const ExternalEvent& e : events) {
        ins.addBindValue(sourceId);
        ins.addBindValue(txt(e.uid));
        ins.addBindValue(txt(e.summary));
        ins.addBindValue(txt(e.location));
        ins.addBindValue(toEpoch(e.start));
        ins.addBindValue(toEpoch(e.end));
        ins.addBindValue(e.allDay ? 1 : 0);
        const auto it = adopted.constFind(e.uid);
        ins.addBindValue(it != adopted.constEnd() ? QVariant(*it) : QVariant());
        ins.exec();
    }
    db.commit();
}

QList<ExternalEvent> Store::externalEventsInRange(const QDateTime& winStart,
                                                  const QDateTime& winEnd)
{
    QList<ExternalEvent> out;
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "SELECT e.id, e.source_id, e.uid, e.summary, e.location_txt, "
        "e.start_utc, e.end_utc, e.all_day, e.adopted_block_id, s.color "
        "FROM external_event e JOIN external_source s ON s.id = e.source_id "
        "WHERE s.enabled = 1 AND e.adopted_block_id IS NULL "
        "AND e.start_utc <= ? AND e.end_utc >= ? "
        "ORDER BY e.start_utc"));
    q.addBindValue(toEpoch(winEnd));
    q.addBindValue(toEpoch(winStart));
    q.exec();
    while (q.next()) {
        ExternalEvent e;
        e.id          = q.value(0).toLongLong();
        e.sourceId    = q.value(1).toLongLong();
        e.uid         = q.value(2).toString();
        e.summary     = q.value(3).toString();
        e.location    = q.value(4).toString();
        e.start       = fromEpoch(q.value(5));
        e.end         = fromEpoch(q.value(6));
        e.allDay      = q.value(7).toInt() != 0;
        e.adoptedBlockId = toOptId(q.value(8));
        e.sourceColor = q.value(9).toString();
        out.append(e);
    }
    return out;
}

void Store::markEventAdopted(Id eventId, Id blockId)
{
    QSqlQuery q(m_db.handle());
    q.prepare(QStringLiteral(
        "UPDATE external_event SET adopted_block_id = ? WHERE id = ?"));
    q.addBindValue(blockId);
    q.addBindValue(eventId);
    q.exec();
}

// ---- rollups ----------------------------------------------------------------

namespace {

// Build a "project_id IN (?,?,..)" clause; appended bind values are the ids.
QString inClause(const QList<Id>& ids)
{
    QStringList marks;
    for (int i = 0; i < ids.size(); ++i)
        marks << QStringLiteral("?");
    return QStringLiteral("project_id IN (%1)").arg(marks.join(QLatin1Char(',')));
}

} // namespace

qint64 Store::plannedMinutes(Id projectId, bool includeDescendants,
                             const QDateTime& winStart, const QDateTime& winEnd)
{
    const QList<Id> ids =
        includeDescendants ? projectAndDescendants(projectId) : QList<Id>{projectId};

    QString sql = QStringLiteral(
                      "SELECT COALESCE(SUM(planned_end - planned_start), 0) "
                      "FROM block WHERE ") + inClause(ids);
    if (winStart.isValid() && winEnd.isValid())
        sql += QStringLiteral(" AND planned_start BETWEEN ? AND ?");

    QSqlQuery q(m_db.handle());
    q.prepare(sql);
    for (Id id : ids)
        q.addBindValue(id);
    if (winStart.isValid() && winEnd.isValid()) {
        q.addBindValue(toEpoch(winStart));
        q.addBindValue(toEpoch(winEnd));
    }
    q.exec();
    return q.next() ? q.value(0).toLongLong() / 60 : 0;
}

qint64 Store::actualMinutes(Id projectId, bool includeDescendants,
                            const QDateTime& winStart, const QDateTime& winEnd)
{
    const QList<Id> ids =
        includeDescendants ? projectAndDescendants(projectId) : QList<Id>{projectId};

    QString sql = QStringLiteral(
                      "SELECT COALESCE(SUM(actual_end - actual_start), 0) "
                      "FROM block WHERE actual_start IS NOT NULL "
                      "AND actual_end IS NOT NULL AND ") + inClause(ids);
    if (winStart.isValid() && winEnd.isValid())
        sql += QStringLiteral(" AND actual_start BETWEEN ? AND ?");

    QSqlQuery q(m_db.handle());
    q.prepare(sql);
    for (Id id : ids)
        q.addBindValue(id);
    if (winStart.isValid() && winEnd.isValid()) {
        q.addBindValue(toEpoch(winStart));
        q.addBindValue(toEpoch(winEnd));
    }
    q.exec();
    return q.next() ? q.value(0).toLongLong() / 60 : 0;
}

} // namespace zb
