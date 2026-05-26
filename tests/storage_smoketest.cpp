// Smoke test for module 0 (storage). Runs the whole model against an in-memory
// SQLite db: nested projects, notes, tasks, a recurring block series, lazy
// occurrence materialization, reconcile, carry-over, the recurrence expander,
// and the planned/actual project rollups. Prints PASS/FAIL per check; exits
// non-zero if anything fails.

#include <QCoreApplication>
#include <QTextStream>

#include "storage/database.h"
#include "storage/recurrence.h"
#include "storage/store.h"

using namespace zb;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    int failed = 0;
    const auto check = [&](const char* what, bool ok) {
        out << "  " << (ok ? "PASS" : "FAIL") << "  " << what << "\n";
        out.flush();
        if (!ok)
            ++failed;
    };

    // --- open + migrate ------------------------------------------------------
    Database db;
    QString error;
    if (!db.open(QStringLiteral(":memory:"), &error)) {
        err << "open failed: " << error << "\n";
        return 1;
    }
    check("schema migrated to v1", db.schemaVersion() == 1);

    Store s(db);

    // --- projects (nested) ---------------------------------------------------
    Project zk;
    zk.name = QStringLiteral("zero_kelvin");
    const Id zkId = s.createProject(zk);

    Project world;
    world.name = QStringLiteral("worldgen");
    world.parentId = zkId;
    const Id worldId = s.createProject(world);

    check("two projects created", zkId > 0 && worldId > 0);
    check("listProjects returns both", s.listProjects().size() == 2);
    check("projectAndDescendants(zk) includes worldgen",
          s.projectAndDescendants(zkId).contains(worldId)
              && s.projectAndDescendants(zkId).size() == 2);
    check("projectAndDescendants(worldgen) is just itself",
          s.projectAndDescendants(worldId).size() == 1);

    // --- note + task in the subproject --------------------------------------
    Note n;
    n.projectId = worldId;
    n.title = QStringLiteral("nebula seeding");
    n.bodyMd = QStringLiteral("# Ideas\n- voronoi sectors\n");
    const Id noteId = s.createNote(n);
    check("note created in worldgen", s.notesForProject(worldId).size() == 1);

    Task t;
    t.projectId = worldId;
    t.title = QStringLiteral("implement sector graph");
    t.estimateMin = 120;
    const Id taskId = s.createTask(t);
    check("task created in worldgen", s.tasksForProject(worldId).size() == 1);

    s.setTaskStatus(taskId, TaskStatus::Done, QDateTime::currentDateTimeUtc());
    check("task marked done", s.tasksForProject(worldId).first().status
                                  == TaskStatus::Done);

    // --- recurrence expander (standalone) ------------------------------------
    {
        const auto r = RRule::parse(
            QStringLiteral("FREQ=WEEKLY;BYDAY=MO,WE,FR;COUNT=12"));
        check("rrule parses", r.has_value());
        check("rrule round-trips",
              r && r->toString()
                       == QStringLiteral("FREQ=WEEKLY;BYDAY=MO,WE,FR;COUNT=12"));

        // Anchor on a Monday (2026-06-01 is a Monday).
        const QDateTime anchor(QDate(2026, 6, 1), QTime(9, 0));
        const QDateTime winStart(QDate(2026, 6, 1), QTime(0, 0));
        const QDateTime winEnd(QDate(2026, 6, 14), QTime(23, 59));
        const auto occ = r->expand(anchor, QDateTime(), winStart, winEnd);
        check("weekly MO/WE/FR over 2 weeks -> 6 occurrences", occ.size() == 6);
        check("first occurrence is the Monday anchor",
              !occ.isEmpty() && occ.first() == anchor);

        // COUNT caps total occurrences from the anchor regardless of window.
        const auto daily = RRule::parse(QStringLiteral("FREQ=DAILY;COUNT=3"));
        const QDateTime farEnd(QDate(2027, 1, 1), QTime(0, 0));
        check("daily COUNT=3 yields exactly 3",
              daily && daily->expand(anchor, QDateTime(), winStart, farEnd).size() == 3);
    }

    // --- recurring block series + lazy materialization -----------------------
    BlockSeries bs;
    bs.projectId = worldId;
    bs.title = QStringLiteral("worldgen focus");
    bs.rrule = QStringLiteral("FREQ=WEEKLY;BYDAY=MO,WE,FR");
    bs.anchorStart = QDateTime(QDate(2026, 6, 1), QTime(9, 0));
    bs.anchorEnd   = QDateTime(QDate(2026, 6, 1), QTime(11, 0)); // 2h blocks
    const Id seriesId = s.createBlockSeries(bs);

    const QDateTime winStart(QDate(2026, 6, 1), QTime(0, 0));
    const QDateTime winEnd(QDate(2026, 6, 14), QTime(23, 59));

    auto occ = s.occurrencesInRange(winStart, winEnd);
    check("6 phantom occurrences before materializing", occ.size() == 6);
    check("all start as phantoms",
          std::none_of(occ.begin(), occ.end(),
                       [](const Occurrence& o) { return o.materialized; }));

    // Materialize the first occurrence (Mon 2026-06-01).
    const Id blockId = s.materializeBlockOccurrence(seriesId, QDate(2026, 6, 1));
    check("materialize returns a block id", blockId > 0);

    occ = s.occurrencesInRange(winStart, winEnd);
    check("still 6 occurrences after materializing (no duplicate)",
          occ.size() == 6);
    int materializedCount = 0;
    for (const auto& o : occ)
        if (o.materialized)
            ++materializedCount;
    check("exactly one occurrence is now materialized", materializedCount == 1);

    // Idempotent: materializing the same slot again returns the same row.
    check("re-materialize is idempotent",
          s.materializeBlockOccurrence(seriesId, QDate(2026, 6, 1)) == blockId);

    // --- reconcile -----------------------------------------------------------
    s.reconcileBlock(blockId,
                     QDateTime(QDate(2026, 6, 1), QTime(9, 15)),
                     QDateTime(QDate(2026, 6, 1), QTime(10, 45)),
                     BlockStatus::Done);
    check("actual time rolls up to 90 minutes (1h30 worked)",
          s.actualMinutes(worldId, false) == 90);
    check("planned time for the materialized block is 120 minutes",
          s.plannedMinutes(worldId, false) == 120);
    check("actual rolls up to parent via descendants",
          s.actualMinutes(zkId, true) == 90);
    check("parent without descendants sees no direct time",
          s.actualMinutes(zkId, false) == 0);
    {
        bool surfaced = false;
        for (const Occurrence& o : s.occurrencesInRange(winStart, winEnd))
            if (o.materialized && o.blockId == blockId)
                surfaced = (o.actualStart && o.actualStart->time() == QTime(9, 15)
                            && o.status == BlockStatus::Done);
        check("reconciled actual surfaces in occurrencesInRange", surfaced);
    }

    // --- carry-over ----------------------------------------------------------
    // A one-off block today that didn't get done -> carry to tomorrow.
    Block one;
    one.projectId = worldId;
    one.title = QStringLiteral("write devlog");
    one.plannedStart = QDateTime(QDate(2026, 6, 2), QTime(16, 0));
    one.plannedEnd   = QDateTime(QDate(2026, 6, 2), QTime(17, 0));
    const Id oneId = s.createBlock(one);
    s.linkBlockTask(oneId, taskId);

    const Id clonedId = s.carryOverBlock(oneId, QDate(2026, 6, 3));
    check("carry-over creates a new block", clonedId > 0 && clonedId != oneId);

    // The original stays put, marked Carried; the clone lands next day, Planned.
    auto wide = s.occurrencesInRange(QDateTime(QDate(2026, 6, 2), QTime(0, 0)),
                                     QDateTime(QDate(2026, 6, 3), QTime(23, 59)));
    bool sawCarriedOrigin = false, sawClone = false;
    for (const auto& o : wide) {
        if (o.blockId == oneId)
            sawCarriedOrigin = (o.status == BlockStatus::Carried
                                && o.occurrenceDate == QDate(2026, 6, 2));
        if (o.blockId == clonedId)
            sawClone = (o.status == BlockStatus::Planned
                        && o.occurrenceDate == QDate(2026, 6, 3));
    }
    check("original block remains on its day marked Carried", sawCarriedOrigin);
    check("clone appears next day as Planned", sawClone);

    // --- block edit / delete (module 3 edit support) ------------------------
    {
        const int before = s.occurrencesInRange(winStart, winEnd).size();
        s.skipOccurrence(seriesId, QDate(2026, 6, 3)); // Wed in the MO/WE/FR series
        const auto after = s.occurrencesInRange(winStart, winEnd);
        const bool wedGone = std::none_of(
            after.begin(), after.end(), [](const Occurrence& o) {
                return o.seriesId && o.plannedStart.date() == QDate(2026, 6, 3);
            });
        check("skipOccurrence hides that day's series occurrence",
              wedGone && after.size() == before - 1);
    }

    s.updateBlockMeta(blockId, QStringLiteral("worldgen focus v2"), worldId);
    {
        bool ok = false;
        for (const Occurrence& o : s.occurrencesInRange(winStart, winEnd))
            if (o.materialized && o.blockId == blockId)
                ok = (o.title == QStringLiteral("worldgen focus v2"));
        check("updateBlockMeta changes the block title", ok);
    }

    s.deleteBlockSeries(seriesId);
    {
        const auto occ3 = s.occurrencesInRange(winStart, winEnd);
        const bool noSeries = std::none_of(
            occ3.begin(), occ3.end(),
            [](const Occurrence& o) { return o.seriesId.has_value(); });
        check("deleteBlockSeries removes all its occurrences", noSeries);
    }

    // --- module-1 CRUD: rename / update / delete -----------------------------
    s.renameProject(worldId, QStringLiteral("worldgen-v2"));
    {
        bool renamed = false;
        for (const Project& p : s.listProjects())
            if (p.id == worldId)
                renamed = (p.name == QStringLiteral("worldgen-v2"));
        check("renameProject persists", renamed);
    }

    s.updateNote(noteId, QStringLiteral("seeding v2"),
                 QStringLiteral("# updated\n- voronoi + lloyd relaxation\n"));
    {
        const Note got = s.note(noteId);
        check("updateNote + note() round-trips",
              got.title == QStringLiteral("seeding v2")
                  && got.bodyMd.contains(QStringLiteral("lloyd")));
    }

    s.deleteNote(noteId);
    check("deleteNote removes it", s.notesForProject(worldId).isEmpty());

    s.updateTask(taskId, QStringLiteral("implement sector graph v2"), 90);
    {
        bool ok = false;
        for (const Task& t : s.tasksForProject(worldId))
            if (t.id == taskId)
                ok = (t.title == QStringLiteral("implement sector graph v2")
                      && t.estimateMin == 90);
        check("updateTask persists title + estimate", ok);
    }
    s.deleteTask(taskId);
    check("deleteTask removes it", s.tasksForProject(worldId).isEmpty());

    // Deleting the parent cascades to the subproject (FK ON DELETE CASCADE).
    s.deleteProject(zkId);
    check("deleteProject cascades to descendants", s.listProjects().isEmpty());

    out << "\nstorage smoke test: "
        << (failed == 0 ? "all passed" : "FAILURES") << "\n";
    return failed == 0 ? 0 : 1;
}
