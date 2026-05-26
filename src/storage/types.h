#pragma once

// Plain data structs mirroring the SQLite schema (see DESIGN.md). No behavior;
// stores in store.h read/write these. Times are QDateTime (stored as UTC epoch
// seconds); occurrence dates are QDate (stored as 'yyyy-MM-dd' text).

#include <QDate>
#include <QDateTime>
#include <QString>
#include <optional>

namespace zb {

using Id = qint64;

enum class TaskStatus { Todo = 0, Doing = 1, Done = 2, Cancelled = 3 };

// A block's lifecycle face. `Planned` until reconciled; `Carried` marks the
// original of a carried-over block (the clone is a fresh `Planned` block).
enum class BlockStatus { Planned = 0, Done = 1, Skipped = 2, Carried = 3 };

struct Project {
    Id                id = -1;
    std::optional<Id> parentId;     // nullopt = top level
    QString           name;
    QString           color;
    bool              archived = false;
    int               sort = 0;
};

struct Note {
    Id        id = -1;
    Id        projectId = -1;        // every note lives in a project
    QString   title;
    QString   bodyMd;
    QDateTime createdAt;
    QDateTime updatedAt;
};

// Recurring task template (e.g. "water plants daily").
struct TaskSeries {
    Id                id = -1;
    Id                projectId = -1;
    QString           title;
    int               estimateMin = 0;
    QString           rrule;         // RFC 5545 subset; see recurrence.h
    QDateTime         anchorDue;     // first occurrence due (serves as DTSTART)
    std::optional<QDateTime> until;
};

struct Task {
    Id                id = -1;
    Id                projectId = -1;
    std::optional<Id> seriesId;      // nullopt = one-off
    QDate             occurrenceDate; // invalid unless materialized from a series
    QString           title;
    TaskStatus        status = TaskStatus::Todo;
    int               estimateMin = 0;
    std::optional<QDateTime> due;
    std::optional<QDateTime> completedAt;
    int               sort = 0;
};

// Recurring block template / routine.
struct BlockSeries {
    Id                id = -1;
    std::optional<Id> projectId;     // canonical time attribution for occurrences
    QString           title;
    QString           color;
    QString           rrule;
    QDateTime         anchorStart;   // first occurrence start (serves as DTSTART)
    QDateTime         anchorEnd;     // first occurrence end (gives time-of-day + duration)
    std::optional<QDateTime> until;
};

// A concrete scheduled span. seriesId set => materialized occurrence of a series.
struct Block {
    Id                id = -1;
    std::optional<Id> seriesId;
    QDate             occurrenceDate; // which slot in the series this fills
    QDateTime         plannedStart;
    QDateTime         plannedEnd;
    std::optional<QDateTime> actualStart; // nullopt = unreconciled
    std::optional<QDateTime> actualEnd;
    std::optional<Id> projectId;
    QString           title;
    QString           color;
    BlockStatus       status = BlockStatus::Planned;
    bool              detached = false;   // instance edited away from its series
    bool              skipped = false;    // this occurrence cancelled
    std::optional<Id> carriedFrom;        // origin block this was carried from
};

// A unified calendar entry for a window: either a persisted Block
// (`materialized`) or a phantom occurrence expanded on the fly from a series.
struct Occurrence {
    bool              materialized = false;
    Id                blockId = -1;       // valid only when materialized
    std::optional<Id> seriesId;
    std::optional<Id> projectId;
    QDate             occurrenceDate;
    QDateTime         plannedStart;
    QDateTime         plannedEnd;
    std::optional<QDateTime> actualStart;
    std::optional<QDateTime> actualEnd;
    QString           title;
    QString           color;
    BlockStatus       status = BlockStatus::Planned;
    bool              skipped = false;
};

} // namespace zb
