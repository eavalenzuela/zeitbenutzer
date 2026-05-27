// Smoke test for module 1 (GUI). Runs under the offscreen platform plugin:
// builds the MainWindow against an in-memory store, verifies the tree reflects
// the projects and that structural changes propagate through a reload. This is
// a construction/wiring smoke, not a pixel test.

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextStream>
#include <QTextTable>

#include "app/adopt_dialog.h"
#include "app/calendar_sources_dialog.h"
#include "app/calendar_view.h"
#include "app/day_review_dialog.h"
#include "app/external_sync.h"
#include "app/main_window.h"
#include "app/markdown_renderer.h"
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

        // A bad source reports a failure rather than failing silently. (Checked
        // before opening the sources dialog, whose live failed() handler would
        // pop a modal box that can't be dismissed headless.)
        ExternalSource bad;
        bad.kind = ExternalSource::Kind::File;
        bad.location = QStringLiteral("/nonexistent/zb-no-such.ics");
        int fails = 0;
        QObject::connect(&sync, &ExternalSync::failed, [&] { ++fails; });
        sync.refreshSource(bad);
        app.processEvents();
        check("a bad source emits failed()", fails == 1);

        // Sources management dialogs construct headless and round-trip a source.
        CalendarSourcesDialog sources(store, sync);
        SourceEditDialog editor;
        ExternalSource probe;
        probe.name = QStringLiteral("My feed");
        probe.location = QStringLiteral("https://example.test/x.ics");
        probe.kind = ExternalSource::Kind::Url;
        editor.setSource(probe);
        app.processEvents();
        check("source editor round-trips its fields",
              editor.source().name == QStringLiteral("My feed")
                  && editor.source().kind == ExternalSource::Kind::Url);
        QFile::remove(path);
    }

    // Markdown converter seam (Phase 1): the renderer owns markdown→HTML; verify
    // the standard constructs still produce the expected HTML and it's robust to
    // empty/edge input. This is the chokepoint later phases hang custom syntax on.
    {
        const QString h = MarkdownRenderer::toHtml(QStringLiteral("# Title"));
        check("renderer emits a heading", h.contains(QStringLiteral("<h1")));
        const QString list =
            MarkdownRenderer::toHtml(QStringLiteral("- one\n- two"));
        check("renderer emits list items", list.contains(QStringLiteral("<li")));
        const QString link = MarkdownRenderer::toHtml(
            QStringLiteral("[x](https://example.test)"));
        check("renderer emits a link", link.contains(QStringLiteral("href")));
        check("renderer preserves prose text",
              MarkdownRenderer::toHtml(QStringLiteral("hello world"))
                  .contains(QStringLiteral("hello world")));
        check("renderer survives empty input",
              !MarkdownRenderer::toHtml(QString()).isEmpty());

        // Parity with the old setMarkdown path on a single inline-formatted line
        // (no lists/quotes/multi-line, which the seam now intentionally diverges
        // on via checkboxes/quote-bar/hard-breaks): inline prose still round-trips.
        const QString sample =
            QStringLiteral("Some **bold**, *italic*, and `code` in one line.");
        QTextDocument direct;
        direct.setMarkdown(sample, QTextDocument::MarkdownDialectGitHub);
        QTextDocument viaSeam;
        viaSeam.setHtml(MarkdownRenderer::toHtml(sample));
        check("HTML seam preserves inline prose content",
              viaSeam.toPlainText() == direct.toPlainText());

        // Hard-wrap preference: a single newline becomes a line break, while a
        // blank line still separates paragraphs.
        QTextDocument hb;
        hb.setHtml(MarkdownRenderer::toHtml(QStringLiteral("first line\nsecond line\n")));
        check("single newline renders as a hard line break",
              hb.toPlainText().contains(QStringLiteral("first line\nsecond line")));

        // Phase 2 — task checkboxes. `- [ ]`/`- [x]` survive the seam as real
        // checkbox markers, and the done glyph is our preferred ☑ (U+2611).
        const QString tasks =
            MarkdownRenderer::toHtml(QStringLiteral("- [ ] todo\n- [x] done\n"));
        QTextDocument tdoc;
        tdoc.setHtml(tasks);
        bool sawUnchecked = false, sawChecked = false;
        for (QTextBlock b = tdoc.begin(); b != tdoc.end(); b = b.next()) {
            const auto m = b.blockFormat().marker();
            sawUnchecked |= (m == QTextBlockFormat::MarkerType::Unchecked);
            sawChecked |= (m == QTextBlockFormat::MarkerType::Checked);
        }
        check("checkboxes render as native task markers through the seam",
              sawUnchecked && sawChecked); // Qt draws ☐/☒ from the marker type

        // Phase 3 — fenced code blocks: one <pre>, preserved indentation, themed
        // keyword color; unknown language degrades to verbatim; blockquotes get
        // the bar + tint. (Runs before the theme switches below, so currentTheme
        // is the default light theme.)
        const QString code = MarkdownRenderer::toHtml(QStringLiteral(
            "```python\ndef f():\n        return 1  # c\n```\n"));
        check("code block renders as a single <pre>",
              code.count(QStringLiteral("<pre")) == 1);
        check("code keyword is colored from the theme",
              code.contains(QStringLiteral("color:%1").arg(lightTheme().synKeyword.name())));
        check("code indentation is preserved",
              code.contains(QStringLiteral("        <span"))); // 8 leading spaces kept
        const QString unk = MarkdownRenderer::toHtml(
            QStringLiteral("```nosuchlang\nx = 1\n```\n"));
        check("unknown language still renders code verbatim",
              unk.contains(QStringLiteral("<pre")) && unk.contains(QStringLiteral("x = 1")));
        check("unterminated fence doesn't drop later content",
              MarkdownRenderer::toHtml(QStringLiteral("```\nstill typing\n"))
                  .contains(QStringLiteral("still typing")));
        const QString quote = MarkdownRenderer::toHtml(QStringLiteral("> quoted line\n"));
        check("blockquote gets a bar glyph and a background tint",
              quote.contains(QString::fromUtf8("▎"))
                  && quote.contains(QStringLiteral("background-color")));

        // Phase 4 — :::csv/:::tsv tables.
        const QString tbl = MarkdownRenderer::toHtml(QStringLiteral(
            ":::csv Cap\nName,Count\n\"a, b\",3\nx,12\n:::\n"));
        check("table renders <table> with header and caption",
              tbl.contains(QStringLiteral("<table")) && tbl.contains(QStringLiteral("<th"))
                  && tbl.contains(QStringLiteral("Cap")));
        check("quoted cell keeps the delimiter inside it",
              tbl.contains(QStringLiteral(">a, b<")));
        check("numeric column is right-aligned",
              tbl.contains(QStringLiteral("text-align:right")));
        // Render-level: Qt actually builds a QTextTable from our HTML.
        QTextDocument tdoc2;
        tdoc2.setHtml(tbl);
        bool hasTable = false;
        for (QTextBlock b = tdoc2.begin(); b != tdoc2.end(); b = b.next()) {
            if (QTextCursor(b).currentTable()) { hasTable = true; break; }
        }
        check("Qt renders the table as a real QTextTable", hasTable);
        check("noheader yields no header cells",
              !MarkdownRenderer::toHtml(QStringLiteral(":::csv noheader\na,b\n:::\n"))
                   .contains(QStringLiteral("<th")));
        const QString semi = MarkdownRenderer::toHtml(
            QStringLiteral(":::csv delim=;\nh1;h2\nx;y\n:::\n"));
        check("delim override splits on the chosen character",
              semi.count(QStringLiteral("<th ")) == 2 && semi.count(QStringLiteral("<td ")) == 2);
        check("ragged rows are padded to the widest row",
              MarkdownRenderer::toHtml(QStringLiteral(":::csv noheader\na,b,c\nx\n:::\n"))
                  .count(QStringLiteral("<td ")) == 6); // 2 rows × 3 cols
        check("unterminated table fence is not rendered as a table",
              !MarkdownRenderer::toHtml(QStringLiteral(":::csv\na,b\n"))
                   .contains(QStringLiteral("<table")));
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
