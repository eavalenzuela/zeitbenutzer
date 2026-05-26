#pragma once

// CRUD over the zeitbenutzer schema plus the two pieces of real logic this
// module owns: lazy materialization of recurring occurrences, and the
// planned/actual time rollups per project. A thin facade over one Database.

#include "storage/database.h"
#include "storage/types.h"

#include <QList>

namespace zb {

class Store {
public:
    explicit Store(Database& db) : m_db(db) {}

    // --- projects ------------------------------------------------------------
    Id            createProject(const Project& p);
    QList<Project> listProjects(bool includeArchived = false);
    // The project plus all transitive descendants (for rolled-up queries).
    QList<Id>     projectAndDescendants(Id rootId);
    void          renameProject(Id projectId, const QString& name);
    void          setProjectColor(Id projectId, const QString& hex);
    // Cascades to descendant projects and their notes/tasks (FK ON DELETE).
    void          deleteProject(Id projectId);

    // --- notes ---------------------------------------------------------------
    Id          createNote(const Note& n);
    QList<Note> notesForProject(Id projectId);
    Note        note(Id noteId);          // id<=0 result if not found
    void        updateNote(Id noteId, const QString& title, const QString& body);
    void        deleteNote(Id noteId);

    // --- tasks ---------------------------------------------------------------
    Id          createTask(const Task& t);
    void        setTaskStatus(Id taskId, TaskStatus status,
                              const QDateTime& completedAt = {});
    void        updateTask(Id taskId, const QString& title, int estimateMin);
    void        deleteTask(Id taskId);
    QList<Task> tasksForProject(Id projectId);

    // --- series --------------------------------------------------------------
    Id createBlockSeries(const BlockSeries& s);
    Id createTaskSeries(const TaskSeries& s);

    // --- blocks --------------------------------------------------------------
    Id           createBlock(const Block& b);
    QList<Block> blocksInRange(const QDateTime& winStart, const QDateTime& winEnd);

    void linkBlockTask(Id blockId, Id taskId);
    void linkBlockNote(Id blockId, Id noteId);

    // Move/resize: update a block's planned span (the calendar drag commit).
    void updateBlockPlanned(Id blockId, const QDateTime& start,
                            const QDateTime& end);
    // Edit a block's title/project attribution.
    void updateBlockMeta(Id blockId, const QString& title,
                         std::optional<Id> projectId);
    void deleteBlock(Id blockId);
    // Delete a series and all of its materialized blocks.
    void deleteBlockSeries(Id seriesId);
    void setBlockSkipped(Id blockId, bool skipped);
    // Hide a single occurrence of a series: materialize it (if needed) + skip.
    void skipOccurrence(Id seriesId, const QDate& date);

    // Persist the series occurrence on `date` as a real block if not already
    // materialized; returns the (existing or new) block id. This is the moment
    // a phantom becomes editable/reconcilable.
    Id materializeBlockOccurrence(Id seriesId, const QDate& date);

    // Set a block's actual span and status — the reconcile step.
    void reconcileBlock(Id blockId, const QDateTime& actualStart,
                        const QDateTime& actualEnd,
                        BlockStatus status = BlockStatus::Done);

    // Clone a block onto `toDate` (same time-of-day, links copied), and mark
    // the original `Carried`. Returns the new block's id. The clone records
    // `carried_from` = original id for the audit trail.
    Id carryOverBlock(Id blockId, const QDate& toDate);

    // --- calendar view -------------------------------------------------------
    // Merge persisted blocks in [winStart,winEnd] with phantom occurrences
    // expanded from every series, de-duplicating on (series, date) so a
    // materialized occurrence never doubles its phantom.
    QList<Occurrence> occurrencesInRange(const QDateTime& winStart,
                                         const QDateTime& winEnd);

    // --- rollups -------------------------------------------------------------
    // Minutes of planned / actual time attributed to a project (optionally
    // including descendant projects) within an optional window. Pass invalid
    // QDateTimes for an all-time total. Only persisted blocks count toward
    // actual; planned counts persisted blocks (phantoms are future intentions
    // surfaced via occurrencesInRange, not committed time).
    qint64 plannedMinutes(Id projectId, bool includeDescendants,
                          const QDateTime& winStart = {},
                          const QDateTime& winEnd = {});
    qint64 actualMinutes(Id projectId, bool includeDescendants,
                         const QDateTime& winStart = {},
                         const QDateTime& winEnd = {});

private:
    Database& m_db;
};

} // namespace zb
