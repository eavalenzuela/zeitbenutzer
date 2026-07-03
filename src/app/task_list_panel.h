#pragma once

// Module 2: the tasks belonging to the selected project. Tasks are first-class
// atoms (title, status, estimate) — distinct from notes, and the pool the
// calendar will later schedule from. New / delete, inline title rename, and a
// detail row (status + estimate) for the selected task.

#include <QWidget>

#include "storage/types.h"

class QComboBox;
class QListWidget;
class QListWidgetItem;
class QSpinBox;

namespace zb {

class Store;

class TaskListPanel : public QWidget {
    Q_OBJECT
public:
    explicit TaskListPanel(Store& store, QWidget* parent = nullptr);

    int taskCount() const; // for tests

public slots:
    void setProject(zb::Id projectId);  // -1 clears
    void reload();

signals:
    // A block was created from a task ("Schedule…") — the calendar should
    // reload to show it.
    void taskScheduled();

private slots:
    void onNewTask();
    void onDeleteTask();
    void onScheduleTask();
    void onItemChanged(QListWidgetItem* item);
    void onCurrentChanged();
    void onStatusChanged(int index);
    void onEstimateChanged();

private:
    Id   selectedTaskId() const;
    int  selectedEstimate() const;
    void styleItem(QListWidgetItem* item, TaskStatus status);
    void setDetailEnabled(bool on);
    void persistOrder();   // write the list's visual order back as task `sort`

    Store&       m_store;
    QListWidget* m_list;
    QComboBox*   m_status;
    QSpinBox*    m_estimate;
    Id           m_projectId = -1;
    bool         m_loading = false;
};

} // namespace zb
