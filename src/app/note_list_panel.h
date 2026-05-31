#pragma once

// Middle pane: the notes belonging to the selected project. New / delete note,
// selection emits noteSelected(id) (-1 == none).

#include <QWidget>

#include "storage/types.h"

class QListWidget;
class QListWidgetItem;

namespace zb {

class Store;

class NoteListPanel : public QWidget {
    Q_OBJECT
public:
    explicit NoteListPanel(Store& store, QWidget* parent = nullptr);

public slots:
    void setProject(zb::Id projectId);   // -1 clears
    void selectNote(zb::Id noteId);      // highlight a note already in the list
    void reload();
    // Refresh the label of a note already in the list (e.g. title edited).
    void updateNoteTitle(zb::Id noteId, const QString& title);

signals:
    void noteSelected(zb::Id noteId);    // -1 == none

private slots:
    void onNewNote();
    void onDeleteNote();
    void onCurrentRowChanged();

private:
    Id   selectedNoteId() const;
    void persistOrder();   // write the list's visual order back as note `sort`

    Store&       m_store;
    QListWidget* m_list;
    Id           m_projectId = -1;
};

} // namespace zb
