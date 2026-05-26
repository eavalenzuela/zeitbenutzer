#pragma once

// Left pane: the project tree. Backed by the Store, rebuilt on structural
// change. Supports create (top-level / child), inline rename, and delete.
// Emits projectSelected(id) where id == -1 means "nothing selected".

#include <QWidget>

#include "storage/types.h"

class QStandardItem;
class QStandardItemModel;
class QTreeView;

namespace zb {

class Store;

class ProjectTreePanel : public QWidget {
    Q_OBJECT
public:
    explicit ProjectTreePanel(Store& store, QWidget* parent = nullptr);

    void reload();
    int  projectCount() const { return m_count; } // for tests

signals:
    void projectSelected(zb::Id projectId); // -1 == none
    void projectsChanged();                  // structure/color changed → refresh

private slots:
    void onNewTopProject();
    void onNewChildProject();
    void onDeleteProject();
    void onSetColor();
    void onContextMenu(const QPoint& pos);
    void onItemChanged(QStandardItem* item);
    void onCurrentChanged();

private:
    Id  selectedProjectId() const;
    void selectProjectById(Id id);

    Store&             m_store;
    QTreeView*         m_view;
    QStandardItemModel* m_model;
    int                m_count = 0;
    bool               m_reloading = false;
};

} // namespace zb
