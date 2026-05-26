#pragma once

// Right pane: edit one note. Title field + markdown body editor with a live
// preview. Saves are debounced; also flushed on note switch and on demand.
// Emits noteTitleChanged so the list pane can relabel without a full reload.

#include <QWidget>

#include "storage/types.h"

class QLineEdit;
class QPlainTextEdit;
class QTextBrowser;
class QTimer;

namespace zb {

class Store;

class NoteEditor : public QWidget {
    Q_OBJECT
public:
    explicit NoteEditor(Store& store, QWidget* parent = nullptr);

    // Persist the in-progress note immediately (call before app exit).
    void flush();
    // Re-apply preview CSS for the current theme (call after a theme change).
    void refreshTheme();

public slots:
    void loadNote(zb::Id noteId);   // -1 clears + disables

signals:
    void noteTitleChanged(zb::Id noteId, const QString& title);

private slots:
    void onEdited();
    void save();

private:
    void setEditingEnabled(bool on);

    Store&          m_store;
    QLineEdit*      m_title;
    QPlainTextEdit* m_body;
    QTextBrowser*   m_preview;
    QTimer*         m_saveTimer;
    Id              m_noteId = -1;
    bool            m_loading = false;
};

} // namespace zb
