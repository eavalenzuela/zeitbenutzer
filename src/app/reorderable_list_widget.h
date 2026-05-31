#pragma once

// A QListWidget that supports drag-to-reorder its rows and reports when a drop
// has rearranged them. Used by the notes and tasks panels, which persist the new
// order on the callback. No Q_OBJECT / signals — a std::function keeps this a
// header-only helper that needs no MOC.

#include <QListWidget>
#include <QDropEvent>
#include <functional>

namespace zb {

class ReorderableListWidget : public QListWidget {
public:
    explicit ReorderableListWidget(QWidget* parent = nullptr)
        : QListWidget(parent)
    {
        setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::MoveAction);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setDragDropOverwriteMode(false);
    }

    // Invoked after a drop has reordered the rows (final order is visible state).
    std::function<void()> onReordered;

protected:
    void dropEvent(QDropEvent* event) override
    {
        QListWidget::dropEvent(event);
        if (event->isAccepted() && onReordered)
            onReordered();
    }
};

} // namespace zb
