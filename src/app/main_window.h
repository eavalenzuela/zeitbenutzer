#pragma once

// Module 1 shell: project tree | notes list | markdown editor, wired together.

#include <QMainWindow>

namespace zb {

class Store;
class ProjectTreePanel;
class NoteListPanel;
class TaskListPanel;
class TimeRollupPanel;
class NoteEditor;
class CalendarView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(Store& store, QWidget* parent = nullptr);

    // Accessors for tests.
    ProjectTreePanel* treePanel() const { return m_tree; }
    NoteListPanel*    notePanel() const { return m_notes; }
    TaskListPanel*    taskPanel() const { return m_tasks; }
    TimeRollupPanel*  timePanel() const { return m_time; }
    CalendarView*     calendarView() const { return m_calendar; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    Store&            m_store;
    ProjectTreePanel* m_tree;
    NoteListPanel*    m_notes;
    TaskListPanel*    m_tasks;
    TimeRollupPanel*  m_time;
    NoteEditor*       m_editor;
    CalendarView*     m_calendar;
};

} // namespace zb
