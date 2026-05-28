#include "app/main_window.h"

#include "app/calendar_sources_dialog.h"
#include "app/calendar_view.h"
#include "app/external_sync.h"
#include "app/markdown_image.h"
#include "app/note_editor.h"
#include "app/note_list_panel.h"
#include "app/project_tree_panel.h"
#include "app/settings.h"
#include "app/settings_dialog.h"
#include "app/syntax_help.h"
#include "app/task_list_panel.h"
#include "app/theme.h"
#include "app/time_rollup_panel.h"
#include "storage/store.h"

#include <QApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTabWidget>

namespace zb {

MainWindow::MainWindow(Store& store, QWidget* parent)
    : QMainWindow(parent), m_store(store)
{
    setWindowTitle(QStringLiteral("zeitbenutzer"));

    m_tree     = new ProjectTreePanel(store, this);
    m_notes    = new NoteListPanel(store, this);
    m_tasks    = new TaskListPanel(store, this);
    m_time     = new TimeRollupPanel(store, this);
    m_editor   = new NoteEditor(store, this);
    m_calendar = new CalendarView(store, this);
    m_sync     = new ExternalSync(store, this);

    // Project workspace: Notes / Tasks / Time tabs alongside the editor.
    auto* middle = new QTabWidget(this);
    middle->addTab(m_notes, QStringLiteral("Notes"));
    middle->addTab(m_tasks, QStringLiteral("Tasks"));
    middle->addTab(m_time, QStringLiteral("Time"));

    auto* workspace = new QSplitter(Qt::Horizontal, this);
    workspace->addWidget(middle);
    workspace->addWidget(m_editor);
    workspace->setStretchFactor(0, 0);
    workspace->setStretchFactor(1, 1);
    workspace->setSizes({280, 600});

    // Center: switch between the Calendar and the project Workspace.
    auto* center = new QTabWidget(this);
    center->addTab(m_calendar, QStringLiteral("Calendar"));
    center->addTab(workspace, QStringLiteral("Workspace"));

    auto* split = new QSplitter(Qt::Horizontal, this);
    split->addWidget(m_tree);
    split->addWidget(center);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({240, 900});
    setCentralWidget(split);

    // Menu bar: Settings + Quit.
    QMenu* appMenu = menuBar()->addMenu(QStringLiteral("&App"));
    appMenu->addAction(QStringLiteral("Settings…"), this, [this] {
        SettingsDialog dlg(m_store, this);
        if (dlg.exec() == QDialog::Accepted) {
            applyTheme(*qApp, themeByName(Settings::instance().themeName()));
            m_editor->refreshTheme();
            m_calendar->applySettings(); // re-aligns week + repaints
            m_time->recompute();
        }
    });
    appMenu->addAction(QStringLiteral("Calendars…"), this, [this] {
        CalendarSourcesDialog dlg(m_store, *m_sync, this);
        dlg.exec();
        m_calendar->reload(); // reflect adds/removes
    });
    appMenu->addAction(QStringLiteral("Reclaim image space"), this, [this] {
        m_editor->flush(); // persist any in-progress note so its refs count
        const int n = sweepOrphanImages(m_store);
        QMessageBox::information(
            this, QStringLiteral("Reclaim image space"),
            n == 0 ? QStringLiteral("No unused images to remove.")
                   : QStringLiteral("Removed %1 unused image(s).").arg(n));
    });
    appMenu->addSeparator();
    appMenu->addAction(QStringLiteral("Quit"), this, &QMainWindow::close);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("Markdown Syntax…"), this,
                        [this] { showSyntaxHelp(this); });

    // Reclaim images orphaned since last run (cheap full scan on a local db).
    sweepOrphanImages(m_store);

    // tree -> {notes, tasks, calendar's default project}; notes -> editor
    connect(m_tree, &ProjectTreePanel::projectSelected, m_notes,
            &NoteListPanel::setProject);
    connect(m_tree, &ProjectTreePanel::projectSelected, m_tasks,
            &TaskListPanel::setProject);
    connect(m_tree, &ProjectTreePanel::projectSelected, m_calendar,
            &CalendarView::setActiveProject);
    connect(m_tree, &ProjectTreePanel::projectSelected, m_time,
            &TimeRollupPanel::setProject);
    connect(m_notes, &NoteListPanel::noteSelected, m_editor,
            &NoteEditor::loadNote);
    // Project structure/color changes → repaint the calendar + rollup.
    connect(m_tree, &ProjectTreePanel::projectsChanged, m_calendar,
            &CalendarView::reload);
    connect(m_tree, &ProjectTreePanel::projectsChanged, m_time,
            &TimeRollupPanel::recompute);
    // editor renames flow back to the list label
    connect(m_editor, &NoteEditor::noteTitleChanged, m_notes,
            &NoteListPanel::updateNoteTitle);
    // Clicking a [[wikilink]] jumps to the target note: select its project (which
    // populates the notes list), then select the note (which loads the editor).
    connect(m_editor, &NoteEditor::noteLinkActivated, this, [this](Id noteId) {
        const Note n = m_store.note(noteId);
        if (n.id <= 0)
            return;
        m_tree->selectProject(n.projectId);
        m_notes->selectNote(noteId);
    });

    // External calendars: repaint when a source's cache updates, and pull all
    // enabled sources once on launch (file reads are synchronous; URL fetches
    // land asynchronously and trigger another reload when they arrive).
    connect(m_sync, &ExternalSync::refreshed, m_calendar,
            &CalendarView::reload);
    // Surface sync problems non-modally (e.g. a bad URL on launch).
    connect(m_sync, &ExternalSync::failed, this, [this](const QString& msg) {
        statusBar()->showMessage(QStringLiteral("Calendar: ") + msg, 8000);
    });
    m_sync->refreshAll();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    m_editor->flush();
    sweepOrphanImages(m_store); // reclaim images no surviving note references
    QMainWindow::closeEvent(event);
}

} // namespace zb
