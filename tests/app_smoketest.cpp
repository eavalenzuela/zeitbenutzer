// Smoke test for module 1 (GUI). Runs under the offscreen platform plugin:
// builds the MainWindow against an in-memory store, verifies the tree reflects
// the projects and that structural changes propagate through a reload. This is
// a construction/wiring smoke, not a pixel test.

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>

#include "app/adopt_dialog.h"
#include "app/calendar_view.h"
#include "app/day_review_dialog.h"
#include "app/external_sync.h"
#include "app/main_window.h"
#include "app/reconcile_panel.h"
#include "app/note_list_panel.h"
#include "app/project_tree_panel.h"
#include "app/task_list_panel.h"
#include "app/theme.h"
#include "app/time_rollup_panel.h"
#include "app/typography.h"
#include "storage/database.h"
#include "storage/store.h"

using namespace zb;

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QTextStream out(stdout);

    int failed = 0;
    const auto check = [&](const char* what, bool ok) {
        out << "  " << (ok ? "PASS" : "FAIL") << "  " << what << "\n";
        out.flush();
        if (!ok)
            ++failed;
    };

    Database db;
    QString error;
    if (!db.open(QStringLiteral(":memory:"), &error)) {
        out << "open failed: " << error << "\n";
        return 1;
    }
    // Bundled fonts must actually load from the Qt resource — a non-"Source"
    // family means the .qrc wasn't linked/initialized (static-lib resource trap).
    registerBundledFonts();
    check("bundled UI font resolved (Source Sans 3)",
          uiFont().family().contains(QStringLiteral("Source")));
    check("bundled editor font resolved (Source Code Pro)",
          editorFont().family().contains(QStringLiteral("Source"))
              && editorFont().fixedPitch());

    Store store(db);

    // Pre-populate one project + note before the window is built.
    Project p;
    p.name = QStringLiteral("alpha");
    const Id pid = store.createProject(p);
    Note n;
    n.projectId = pid;
    n.title = QStringLiteral("first note");
    store.createNote(n);

    MainWindow w(store);
    w.show();
    app.processEvents();

    check("window constructs and shows", w.isVisible());
    check("tree reflects the pre-existing project",
          w.treePanel()->projectCount() == 1);

    // A child added through the store appears after reload.
    Project child;
    child.name = QStringLiteral("alpha-child");
    child.parentId = pid;
    store.createProject(child);
    w.treePanel()->reload();
    app.processEvents();
    check("tree picks up a newly added child", w.treePanel()->projectCount() == 2);

    // Selecting the project should drive the notes list without crashing.
    w.notePanel()->setProject(pid);
    app.processEvents();
    check("selecting a project populates notes without crash", true);

    // Tasks: a task added through the store shows in the task panel.
    Task t;
    t.projectId = pid;
    t.title = QStringLiteral("first task");
    store.createTask(t);
    w.taskPanel()->setProject(pid);
    app.processEvents();
    check("task panel reflects the project's tasks",
          w.taskPanel()->taskCount() == 1);

    // Calendar: a block planned this week shows up as an occurrence.
    Block b;
    b.projectId = pid;
    b.title = QStringLiteral("planning");
    const QDateTime base(QDate::currentDate(), QTime(9, 0));
    b.plannedStart = base;
    b.plannedEnd = base.addSecs(3600);
    b.status = BlockStatus::Planned;
    store.createBlock(b);
    w.calendarView()->reload();
    app.processEvents();
    const int afterBlock = w.calendarView()->occurrenceCount();
    check("calendar shows a block planned in the current week", afterBlock >= 1);

    // A recurring series adds phantom occurrences across the week.
    BlockSeries ser;
    ser.projectId = pid;
    ser.title = QStringLiteral("daily standup");
    ser.rrule = QStringLiteral("FREQ=DAILY");
    const QDateTime sStart(QDate::currentDate(), QTime(8, 0));
    ser.anchorStart = sStart;
    ser.anchorEnd = sStart.addSecs(900);
    store.createBlockSeries(ser);
    w.calendarView()->reload();
    app.processEvents();
    check("recurring series renders phantom occurrences",
          w.calendarView()->occurrenceCount() > afterBlock);

    // Time rollup: the 60-min block planned today shows in this-week planned.
    w.timePanel()->setProject(pid);
    app.processEvents();
    check("time rollup reports this-week planned minutes",
          w.timePanel()->weekPlanned() >= 60);

    // Reconcile + the guided evening-review walk.
    {
        const QDate today = QDate::currentDate();
        const QDate tomorrow = today.addDays(1);
        const QDateTime ds(today, QTime(0, 0)), de(today, QTime(23, 59, 59));

        // Grab today's phantom standup occurrence (a series slot, not yet
        // materialized) and carry it over via the shared helper.
        Occurrence phantom;
        bool found = false;
        for (const Occurrence& o : store.occurrencesInRange(ds, de)) {
            if (o.seriesId && !o.materialized) {
                phantom = o;
                found = true;
                break;
            }
        }
        check("a phantom occurrence is available to reconcile", found);

        const Id carriedId = applyReconcile(store, phantom, BlockStatus::Carried,
                                            QDateTime(), QDateTime(), tomorrow);
        check("applyReconcile(Carried) materializes the origin block",
              carriedId > 0);

        const auto span = store.occurrencesInRange(
            ds, QDateTime(tomorrow, QTime(23, 59, 59)));
        bool originCarried = false, cloneNextDay = false;
        for (const auto& o : span) {
            if (o.blockId == carriedId)
                originCarried = (o.status == BlockStatus::Carried
                                 && o.occurrenceDate == today);
            else if (o.materialized && o.occurrenceDate == tomorrow
                     && o.status == BlockStatus::Planned)
                cloneNextDay = true;
        }
        check("origin stays on its day marked Carried", originCarried);
        check("a Planned clone lands on the next day", cloneNextDay);

        // The review walk consumes the day's still-unreconciled occurrences.
        QList<Occurrence> queue;
        for (const Occurrence& o : store.occurrencesInRange(ds, de))
            if (o.status == BlockStatus::Planned && !o.skipped)
                queue.append(o);
        check("review queue holds today's unreconciled blocks",
              !queue.isEmpty());
        DayReviewDialog review(store, queue);
        app.processEvents();
        check("review dialog constructs headless", true);
    }

    // External calendars: sync a local .ics file source through ExternalSync.
    {
        const QByteArray stamp =
            QDate::currentDate().toString(QStringLiteral("yyyyMMdd")).toUtf8();
        const QByteArray ics =
            "BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"
            "BEGIN:VEVENT\r\nUID:sync-1\r\nSUMMARY:Imported event\r\n"
            "DTSTART:" + stamp + "T130000Z\r\n"
            "DTEND:" + stamp + "T140000Z\r\n"
            "END:VEVENT\r\n"
            "BEGIN:VEVENT\r\nUID:allday-sync\r\nSUMMARY:All-day import\r\n"
            "DTSTART;VALUE=DATE:" + stamp + "\r\n"
            "END:VEVENT\r\nEND:VCALENDAR\r\n";
        const QString path =
            QDir(QDir::tempPath()).filePath(QStringLiteral("zb_sync_test.ics"));
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(ics);
        f.close();

        ExternalSource src;
        src.kind = ExternalSource::Kind::File;
        src.location = path;
        src.name = QStringLiteral("Local ICS");
        store.createExternalSource(src);

        ExternalSync sync(store);
        int refreshes = 0;
        QObject::connect(&sync, &ExternalSync::refreshed,
                         [&] { ++refreshes; });
        sync.refreshAll();
        app.processEvents();
        check("file-source sync emits refreshed", refreshes >= 1);

        const QDate today = QDate::currentDate();
        const QDateTime ws(today.addDays(-1), QTime(0, 0));
        const QDateTime we(today.addDays(1), QTime(23, 59, 59));
        check("synced events are cached and queryable",
              store.externalEventsInRange(ws, we).size() == 2);

        // The calendar overlay: an all-day event opens the all-day band.
        const double bandBefore = w.calendarView()->allDayBandHeight();
        w.calendarView()->reload();
        app.processEvents();
        check("all-day external event opens the all-day band",
              bandBefore == 0.0 && w.calendarView()->allDayBandHeight() > 0.0);

        // Adopt dialog constructs headless.
        AdoptDialog adopt(store, QStringLiteral("All-day import"));
        app.processEvents();
        check("adopt dialog constructs headless", true);
        QFile::remove(path);
    }

    // Themes + project colors.
    applyTheme(app, darkTheme());
    check("dark theme applies", currentTheme().name == QStringLiteral("dark"));
    applyTheme(app, lightTheme());
    check("theme by name resolves dark",
          themeByName(QStringLiteral("dark")).name == QStringLiteral("dark"));
    check("derived project color is valid", projectColor(QString(), 2).isValid());
    check("explicit project color honored",
          projectColor(QStringLiteral("#ff0000"), 2) == QColor(255, 0, 0));

    out << "\napp smoke test: " << (failed == 0 ? "all passed" : "FAILURES")
        << "\n";
    return failed == 0 ? 0 : 1;
}
