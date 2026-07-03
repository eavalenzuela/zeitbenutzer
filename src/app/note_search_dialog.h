#pragma once

// Find in notes: a live-filtered search over note titles and bodies
// (Store::searchNotes). Activating a result accepts the dialog with the
// chosen note id; the main window then jumps to it through the same
// project-select → note-select path wikilinks use.

#include <QDialog>

#include "storage/types.h"

class QLineEdit;
class QListWidget;

namespace zb {

class Store;

class NoteSearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit NoteSearchDialog(Store& store, QWidget* parent = nullptr);

    Id chosenNoteId() const { return m_chosen; } // -1 = nothing chosen

    void setQuery(const QString& q); // for tests (drives the live search)
    int  resultCount() const;        // for tests

private slots:
    void onQueryChanged(const QString& text);
    void onActivated();

private:
    Store&       m_store;
    QLineEdit*   m_query;
    QListWidget* m_results;
    Id           m_chosen = -1;
};

} // namespace zb
