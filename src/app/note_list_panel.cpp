#include "app/note_list_panel.h"

#include "storage/store.h"

#include <QListWidget>
#include <QToolBar>
#include <QVBoxLayout>

namespace zb {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
}

NoteListPanel::NoteListPanel(Store& store, QWidget* parent)
    : QWidget(parent), m_store(store)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* bar = new QToolBar(this);
    bar->addAction(QStringLiteral("New note"), this, &NoteListPanel::onNewNote);
    bar->addAction(QStringLiteral("Delete note"), this,
                   &NoteListPanel::onDeleteNote);
    layout->addWidget(bar);

    m_list = new QListWidget(this);
    layout->addWidget(m_list);

    connect(m_list, &QListWidget::currentRowChanged, this,
            &NoteListPanel::onCurrentRowChanged);
}

void NoteListPanel::setProject(Id projectId)
{
    m_projectId = projectId;
    reload();
}

void NoteListPanel::reload()
{
    m_list->clear();
    if (m_projectId > 0) {
        for (const Note& n : m_store.notesForProject(m_projectId)) {
            auto* item = new QListWidgetItem(
                n.title.isEmpty() ? QStringLiteral("(untitled)") : n.title);
            item->setData(kIdRole, static_cast<qlonglong>(n.id));
            m_list->addItem(item);
        }
    }
    // currentRowChanged fires on clear/populate; ensure a clean "none" state.
    if (m_list->count() == 0)
        emit noteSelected(-1);
}

void NoteListPanel::updateNoteTitle(Id noteId, const QString& title)
{
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* item = m_list->item(i);
        if (item->data(kIdRole).toLongLong() == noteId) {
            item->setText(title.isEmpty() ? QStringLiteral("(untitled)") : title);
            return;
        }
    }
}

Id NoteListPanel::selectedNoteId() const
{
    QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(kIdRole).toLongLong() : -1;
}

void NoteListPanel::selectNote(Id noteId)
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(kIdRole).toLongLong() == noteId) {
            m_list->setCurrentRow(i); // emits noteSelected via onCurrentRowChanged
            return;
        }
    }
}

void NoteListPanel::onNewNote()
{
    if (m_projectId <= 0)
        return;
    Note n;
    n.projectId = m_projectId;
    n.title = QStringLiteral("Untitled");
    const Id id = m_store.createNote(n);
    reload();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(kIdRole).toLongLong() == id) {
            m_list->setCurrentRow(i);
            break;
        }
    }
}

void NoteListPanel::onDeleteNote()
{
    const Id id = selectedNoteId();
    if (id <= 0)
        return;
    m_store.deleteNote(id);
    reload();
}

void NoteListPanel::onCurrentRowChanged()
{
    emit noteSelected(selectedNoteId());
}

} // namespace zb
