#include "app/note_editor.h"

#include "app/theme.h"
#include "app/typography.h"
#include "storage/store.h"

#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace zb {

NoteEditor::NoteEditor(Store& store, QWidget* parent)
    : QWidget(parent), m_store(store)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    m_title = new QLineEdit(this);
    m_title->setPlaceholderText(QStringLiteral("Title"));
    layout->addWidget(m_title);

    auto* split = new QSplitter(Qt::Horizontal, this);
    m_body = new QPlainTextEdit(split);
    m_body->setPlaceholderText(QStringLiteral("Write in Markdown…"));
    m_body->setFont(editorFont()); // monospace source of truth
    m_preview = new QTextBrowser(split);
    m_preview->setOpenExternalLinks(true);
    m_preview->setFont(uiFont());  // proportional rendered prose
    stylePreview(m_preview, currentTheme()); // code spans → mono, themed links
    split->addWidget(m_body);
    split->addWidget(m_preview);
    split->setSizes({1, 1});
    layout->addWidget(split, 1);

    // Debounced autosave: coalesce keystrokes, write ~700ms after the last one.
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(700);
    connect(m_saveTimer, &QTimer::timeout, this, &NoteEditor::save);

    connect(m_title, &QLineEdit::textChanged, this, &NoteEditor::onEdited);
    connect(m_body, &QPlainTextEdit::textChanged, this, &NoteEditor::onEdited);

    setEditingEnabled(false);
}

void NoteEditor::setEditingEnabled(bool on)
{
    m_title->setEnabled(on);
    m_body->setEnabled(on);
}

void NoteEditor::loadNote(Id noteId)
{
    // Persist whatever was being edited before switching away.
    flush();

    m_noteId = noteId;
    m_loading = true;

    if (noteId <= 0) {
        m_title->clear();
        m_body->clear();
        m_preview->clear();
        setEditingEnabled(false);
    } else {
        const Note n = m_store.note(noteId);
        m_title->setText(n.title);
        m_body->setPlainText(n.bodyMd);
        m_preview->setMarkdown(n.bodyMd);
        setEditingEnabled(true);
    }
    m_loading = false;
}

void NoteEditor::onEdited()
{
    if (m_loading || m_noteId <= 0)
        return;
    m_preview->setMarkdown(m_body->toPlainText());
    m_saveTimer->start(); // (re)arm debounce
}

void NoteEditor::save()
{
    if (m_noteId <= 0)
        return;
    const QString title = m_title->text();
    m_store.updateNote(m_noteId, title, m_body->toPlainText());
    emit noteTitleChanged(m_noteId, title);
}

void NoteEditor::flush()
{
    if (m_saveTimer->isActive()) {
        m_saveTimer->stop();
        save();
    }
}

void NoteEditor::refreshTheme()
{
    stylePreview(m_preview, currentTheme());
    if (m_noteId > 0)
        m_preview->setMarkdown(m_body->toPlainText()); // re-render with new CSS
}

} // namespace zb
