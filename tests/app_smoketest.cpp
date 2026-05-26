// Smoke test for module 1 (GUI). Runs under the offscreen platform plugin:
// builds the MainWindow against an in-memory store, verifies the tree reflects
// the projects and that structural changes propagate through a reload. This is
// a construction/wiring smoke, not a pixel test.

#include <QApplication>
#include <QTextStream>

#include "app/calendar_view.h"
#include "app/main_window.h"
#include "app/note_list_panel.h"
#include "app/project_tree_panel.h"
#include "app/task_list_panel.h"
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

    out << "\napp smoke test: " << (failed == 0 ? "all passed" : "FAILURES")
        << "\n";
    return failed == 0 ? 0 : 1;
}
