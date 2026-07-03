// Smoke test for module 0 (storage). Runs the whole model against an in-memory
// SQLite db: nested projects, notes, tasks, a recurring block series, lazy
// occurrence materialization, reconcile, carry-over, the recurrence expander,
// and the planned/actual project rollups. Prints PASS/FAIL per check; exits
// non-zero if anything fails.

#include <QCoreApplication>
#include <QTextStream>

#include "storage/database.h"
#include "storage/ical.h"
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
    check("schema migrated to v4", db.schemaVersion() == 4);

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

    // --- manual reordering (notes / tasks / projects) ------------------------
    {
        // Three notes append in creation order; setNoteOrder flips them.
        Note a, b, c;
        a.projectId = b.projectId = c.projectId = zkId;
        a.title = QStringLiteral("a");
        b.title = QStringLiteral("b");
        c.title = QStringLiteral("c");
        const Id ia = s.createNote(a), ib = s.createNote(b), ic = s.createNote(c);
        auto titles = [&] {
            QStringList out;
            for (const Note& nn : s.notesForProject(zkId))
                out << nn.title;
            return out;
        };
        check("notes append in creation order",
              titles() == QStringList({"a", "b", "c"}));
        s.setNoteOrder({ic, ia, ib});
        check("setNoteOrder reorders", titles() == QStringList({"c", "a", "b"}));

        // Tasks: same append-then-reorder.
        Task t1, t2;
        t1.projectId = t2.projectId = zkId;
        t1.title = QStringLiteral("t1");
        t2.title = QStringLiteral("t2");
        const Id k1 = s.createTask(t1), k2 = s.createTask(t2);
        check("tasks append in creation order",
              s.tasksForProject(zkId).first().id == k1);
        s.setTaskOrder({k2, k1});
        check("setTaskOrder reorders",
              s.tasksForProject(zkId).first().id == k2);

        // Projects: re-parent worldgen to top level + place it before zk.
        s.setProjectPositions({{worldId, std::nullopt, 0}, {zkId, std::nullopt, 1}});
        const auto projs = s.listProjects();
        check("setProjectPositions re-parents to top level",
              !projs.first().parentId.has_value()
                  && projs.first().id == worldId);
        check("projectAndDescendants(zk) no longer includes worldgen",
              s.projectAndDescendants(zkId).size() == 1);
        // Restore the original hierarchy for the assertions that follow.
        s.setProjectPositions({{zkId, std::nullopt, 0}, {worldId, zkId, 0}});
        check("hierarchy restored",
              s.projectAndDescendants(zkId).size() == 2);
        // Clean up the extra notes/tasks so later counts stay as written.
        for (Id id : {ia, ib, ic})
            s.deleteNote(id);
        for (Id id : {k1, k2})
            s.deleteTask(id);
    }

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

        // Fast-forward: a window far past the anchor must keep the pattern
        // alignment. INTERVAL=2 weekly from a Monday decades back → exactly
        // one hit in a two-week window, on a Monday, a whole number of
        // 2-week periods from the anchor.
        const auto biweekly =
            RRule::parse(QStringLiteral("FREQ=WEEKLY;INTERVAL=2"));
        const QDateTime oldAnchor(QDate(2000, 1, 3), QTime(9, 0)); // a Monday
        const auto far = biweekly->expand(oldAnchor, QDateTime(),
                                          winStart, winEnd);
        check("biweekly far-window expansion yields one aligned Monday",
              far.size() == 1 && far.first().date().dayOfWeek() == 1
                  && QDate(2000, 1, 3).daysTo(far.first().date()) % 14 == 0);

        // Same alignment invariant for an every-3-days rule…
        const auto every3 =
            RRule::parse(QStringLiteral("FREQ=DAILY;INTERVAL=3"));
        const auto far3 = every3->expand(oldAnchor, QDateTime(),
                                         winStart, winEnd);
        check("every-3-days far-window expansion stays anchor-aligned",
              !far3.isEmpty()
                  && QDate(2000, 1, 3).daysTo(far3.first().date()) % 3 == 0
                  && far3.first() >= winStart);

        // …and for a bimonthly BYMONTHDAY rule.
        const auto biMonthly = RRule::parse(
            QStringLiteral("FREQ=MONTHLY;INTERVAL=2;BYMONTHDAY=15"));
        const QDateTime mWin1(QDate(2026, 6, 1), QTime(0, 0));
        const QDateTime mWin2(QDate(2026, 7, 31), QTime(23, 59));
        const auto farM = biMonthly->expand(oldAnchor, QDateTime(), mWin1, mWin2);
        check("bimonthly far-window expansion yields one aligned 15th",
              farM.size() == 1 && farM.first().date().day() == 15
                  && ((farM.first().date().year() - 2000) * 12
                      + farM.first().date().month() - 1) % 2 == 0);
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

    // --- external calendars (module 6): parse, expand, cache, adopt ----------
    {
        const QByteArray ics =
            "BEGIN:VCALENDAR\r\n"
            "VERSION:2.0\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:utc-1\r\n"
            "SUMMARY:UTC meeting\r\n"
            "DTSTART:20260603T090000Z\r\n"
            "DTEND:20260603T100000Z\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:tz-1\r\n"
            "SUMMARY:Zoned lunch\r\n"
            "DTSTART;TZID=America/New_York:20260603T120000\r\n"
            "DTEND;TZID=America/New_York:20260603T130000\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:allday-1\r\n"
            "SUMMARY:Conference\r\n"
            "DESCRIPTION:folded line\r\n"
            " continues here\r\n"
            "DTSTART;VALUE=DATE:20260604\r\n"
            "DTEND;VALUE=DATE:20260605\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\n"
            "UID:weekly-1\r\n"
            "SUMMARY:Standup\r\n"
            "DTSTART:20260601T140000Z\r\n"
            "DTEND:20260601T141500Z\r\n"
            "RRULE:FREQ=WEEKLY\r\n"
            "EXDATE:20260608T140000Z\r\n"
            "END:VEVENT\r\n"
            "END:VCALENDAR\r\n";

        QString perr;
        const QList<ICalEvent> parsed = parseICalendar(ics, &perr);
        check("parse yields four VEVENTs", parsed.size() == 4 && perr.isEmpty());

        // TZID resolution: 12:00 America/New_York on 2026-06-03 (EDT, UTC-4) = 16:00Z.
        bool tzOk = false, allDayOk = false;
        for (const ICalEvent& e : parsed) {
            if (e.uid == QStringLiteral("tz-1"))
                tzOk = (e.start.toUTC().time() == QTime(16, 0));
            if (e.uid == QStringLiteral("allday-1"))
                allDayOk = e.allDay;
        }
        check("TZID start converts to the right UTC instant", tzOk);
        check("VALUE=DATE event is flagged all-day", allDayOk);

        const QDateTime jun1(QDate(2026, 6, 1), QTime(0, 0));
        const QDateTime jul1(QDate(2026, 6, 30), QTime(23, 59));
        const QList<ICalEvent> expanded = expandICalEvents(parsed, jun1, jul1);
        // 3 one-off + 4 weekly Mondays (Jun 8 dropped by EXDATE) = 7.
        check("expansion drops the EXDATE occurrence (7 instances)",
              expanded.size() == 7);
        const bool noJun8 = std::none_of(
            expanded.begin(), expanded.end(), [](const ICalEvent& e) {
                return e.start.toUTC().date() == QDate(2026, 6, 8);
            });
        check("no instance lands on the excluded date", noJun8);

        // Cache into a source and read it back through the store.
        ExternalSource src;
        src.kind = ExternalSource::Kind::Url;
        src.location = QStringLiteral("https://example.test/cal.ics");
        src.name = QStringLiteral("Work");
        const Id srcId = s.createExternalSource(src);
        check("external source persists",
              s.listExternalSources().size() == 1
                  && s.listExternalSources().first().id == srcId);

        QList<ExternalEvent> evs;
        for (const ICalEvent& ie : expanded) {
            ExternalEvent e;
            e.uid = ie.uid;
            e.summary = ie.summary;
            e.location = ie.location;
            e.start = ie.start;
            e.end = ie.end;
            e.allDay = ie.allDay;
            evs.append(e);
        }
        s.replaceSourceEvents(srcId, evs);
        const auto cached = s.externalEventsInRange(jun1, jul1);
        check("cached events round-trip through the store", cached.size() == 7);
        const bool allDayPersisted = std::any_of(
            cached.begin(), cached.end(), [](const ExternalEvent& e) {
                return e.uid == QStringLiteral("allday-1") && e.allDay;
            });
        check("all-day flag survives the round-trip", allDayPersisted);

        // Adopt the all-day event into a block; it then drops from the overlay.
        Id adoptEventId = -1;
        for (const ExternalEvent& e : cached)
            if (e.uid == QStringLiteral("allday-1"))
                adoptEventId = e.id;
        Block adopted;
        adopted.title = QStringLiteral("Conference (adopted)");
        adopted.plannedStart = QDateTime(QDate(2026, 6, 4), QTime(0, 0));
        adopted.plannedEnd = QDateTime(QDate(2026, 6, 5), QTime(0, 0));
        const Id adoptBlockId = s.createBlock(adopted);
        s.markEventAdopted(adoptEventId, adoptBlockId);
        check("adopted event leaves the overlay",
              s.externalEventsInRange(jun1, jul1).size() == 6);

        // Re-sync must preserve the adoption (matched on uid).
        s.replaceSourceEvents(srcId, evs);
        check("re-sync keeps the adopted event suppressed",
              s.externalEventsInRange(jun1, jul1).size() == 6);
    }

    // --- archiving (cascade + list filtering) ---------------------------------
    {
        Project parent, kid;
        parent.name = QStringLiteral("parked");
        const Id parentId = s.createProject(parent);
        kid.name = QStringLiteral("parked-child");
        kid.parentId = parentId;
        const Id kidId = s.createProject(kid);

        s.setProjectArchived(parentId, true);
        check("archiving cascades to descendants and hides both",
              s.listProjects(false).isEmpty());
        const auto all = s.listProjects(true);
        check("archived projects still list with includeArchived",
              all.size() == 2
                  && std::all_of(all.begin(), all.end(),
                                 [](const Project& p) { return p.archived; }));

        s.setProjectArchived(parentId, false);
        check("unarchiving restores the whole subtree",
              s.listProjects(false).size() == 2);
        s.deleteProject(parentId);
        check("archive scratch projects removed", s.listProjects(true).isEmpty());
        (void)kidId;
    }

    // --- note search -----------------------------------------------------------
    {
        Project sp;
        sp.name = QStringLiteral("search_project");
        const Id spId = s.createProject(sp);
        Note a, b;
        a.projectId = b.projectId = spId;
        a.title = QStringLiteral("Voronoi ideas");
        a.bodyMd = QStringLiteral("relaxation passes");
        b.title = QStringLiteral("shopping");
        b.bodyMd = QStringLiteral("50% off lloyd brand tea");
        s.createNote(a);
        s.createNote(b);

        check("searchNotes matches titles (case-insensitive)",
              s.searchNotes(QStringLiteral("voronoi")).size() == 1);
        check("searchNotes matches bodies",
              s.searchNotes(QStringLiteral("lloyd")).size() == 1);
        check("searchNotes treats LIKE metacharacters literally",
              s.searchNotes(QStringLiteral("50%")).size() == 1
                  && s.searchNotes(QStringLiteral("5%f")).isEmpty());
        check("searchNotes with no hits returns empty",
              s.searchNotes(QStringLiteral("no-such-text")).isEmpty());
        check("blank query returns nothing",
              s.searchNotes(QStringLiteral("   ")).isEmpty());
        s.deleteProject(spId); // cascades to the notes
    }

    // --- rollup guard: a malformed span can't subtract time --------------------
    {
        Project rp;
        rp.name = QStringLiteral("rollup_guard");
        const Id rpId = s.createProject(rp);
        Block good, bad;
        good.projectId = bad.projectId = rpId;
        good.title = QStringLiteral("good");
        good.plannedStart = QDateTime(QDate(2026, 6, 8), QTime(9, 0));
        good.plannedEnd   = QDateTime(QDate(2026, 6, 8), QTime(10, 0));
        const Id goodId = s.createBlock(good);
        s.reconcileBlock(goodId, good.plannedStart, good.plannedEnd);

        bad.title = QStringLiteral("bad");
        bad.plannedStart = QDateTime(QDate(2026, 6, 8), QTime(11, 0));
        bad.plannedEnd   = QDateTime(QDate(2026, 6, 8), QTime(12, 0));
        const Id badId = s.createBlock(bad);
        // Simulate a corrupt row: actual end before actual start.
        s.reconcileBlock(badId, QDateTime(QDate(2026, 6, 8), QTime(23, 0)),
                         QDateTime(QDate(2026, 6, 8), QTime(22, 0)));

        check("negative actual span is clamped out of the rollup",
              s.actualMinutes(rpId, false) == 60);
        s.deleteProject(rpId);
    }

    // --- ICS writer round-trip ---------------------------------------------------
    {
        ICalEvent timed;
        timed.uid = QStringLiteral("zb-block-7@zeitbenutzer");
        timed.summary = QStringLiteral("plan; review, then\nship");
        timed.location = QStringLiteral("desk");
        timed.start = QDateTime(QDate(2026, 6, 8), QTime(9, 0), QTimeZone::utc());
        timed.end   = QDateTime(QDate(2026, 6, 8), QTime(10, 30), QTimeZone::utc());

        ICalEvent allday;
        allday.uid = QStringLiteral("zb-allday-1@zeitbenutzer");
        allday.summary = QStringLiteral("Offsite");
        allday.allDay = true;
        allday.start = QDateTime(QDate(2026, 6, 9), QTime(0, 0));
        allday.end   = QDateTime(QDate(2026, 6, 10), QTime(0, 0));

        const QByteArray ics = writeICalendar({timed, allday});
        QString werr;
        const QList<ICalEvent> back = parseICalendar(ics, &werr);
        check("written ICS parses back to two events",
              back.size() == 2 && werr.isEmpty());
        bool timedOk = false, alldayOk = false;
        for (const ICalEvent& e : back) {
            if (e.uid == timed.uid)
                timedOk = (e.summary == timed.summary
                           && e.location == timed.location
                           && e.start == timed.start && e.end == timed.end
                           && !e.allDay);
            if (e.uid == allday.uid)
                alldayOk = (e.allDay
                            && e.start.date() == QDate(2026, 6, 9)
                            && e.end.date() == QDate(2026, 6, 10));
        }
        check("timed event round-trips (escaped text, UTC span)", timedOk);
        check("all-day event round-trips (VALUE=DATE, exclusive end)", alldayOk);
    }

    // --- images (markdown embeds) --------------------------------------------
    {
        const QByteArray png("\x89PNG\r\n\x1a\nfake-bytes", 18);
        const Id img1 = s.putImage(QStringLiteral("sha-aaa"),
                                   QStringLiteral("image/png"), png, QString());
        check("putImage inserts a blob image", img1 > 0);

        // Same sha → dedup to the same row, not a second insert.
        const Id dup = s.putImage(QStringLiteral("sha-aaa"),
                                  QStringLiteral("image/png"), png, QString());
        check("putImage dedups by sha256", dup == img1);
        check("only one image row exists", s.imageIds().size() == 1);

        const Image got = s.image(img1);
        check("image round-trips bytes + mime",
              got.bytes == png && got.mime == QStringLiteral("image/png")
                  && got.path.isEmpty());

        // Migrate it to the disk backend (bytes → path).
        s.setImageStorage(img1, QByteArray(), QStringLiteral("images/aaa.png"));
        const Image moved = s.image(img1);
        check("setImageStorage repoints blob → disk",
              moved.bytes.isEmpty() && moved.path == QStringLiteral("images/aaa.png"));

        // A disk-backed second image, then delete it.
        const Id img2 = s.putImage(QStringLiteral("sha-bbb"),
                                   QStringLiteral("image/jpeg"), QByteArray(),
                                   QStringLiteral("images/bbb.jpg"));
        check("two distinct images stored", s.imageIds().size() == 2 && img2 != img1);
        s.deleteImage(img2);
        check("deleteImage removes the row", s.imageIds().size() == 1);

        // allNoteBodies feeds the orphan sweep (app layer parses zb-img tokens).
        Project ip;
        ip.name = QStringLiteral("img_project");
        const Id ipId = s.createProject(ip);
        Note n;
        n.projectId = ipId;
        n.title = QStringLiteral("with image");
        n.bodyMd = QStringLiteral("see ![x](zb-img:%1)").arg(img1);
        s.createNote(n);
        bool found = false;
        for (const QString& body : s.allNoteBodies())
            if (body.contains(QStringLiteral("zb-img:%1").arg(img1)))
                found = true;
        check("allNoteBodies surfaces image references", found);
    }

    out << "\nstorage smoke test: "
        << (failed == 0 ? "all passed" : "FAILURES") << "\n";
    return failed == 0 ? 0 : 1;
}
