#include "app/note_search_dialog.h"

#include "storage/store.h"

#include <QHash>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>

namespace zb {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
} // namespace

NoteSearchDialog::NoteSearchDialog(Store& store, QWidget* parent)
    : QDialog(parent), m_store(store)
{
    setWindowTitle(QStringLiteral("Find in notes"));
    resize(460, 360);

    m_query = new QLineEdit(this);
    m_query->setPlaceholderText(QStringLiteral("Search titles and bodies…"));
    m_query->setClearButtonEnabled(true);

    m_results = new QListWidget(this);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_query);
    layout->addWidget(m_results, 1);

    connect(m_query, &QLineEdit::textChanged, this,
            &NoteSearchDialog::onQueryChanged);
    // Enter in the query box picks the current (or first) result; a
    // double-click / Enter on the list picks that row.
    connect(m_query, &QLineEdit::returnPressed, this,
            &NoteSearchDialog::onActivated);
    connect(m_results, &QListWidget::itemActivated, this,
            &NoteSearchDialog::onActivated);

    m_query->setFocus();
}

void NoteSearchDialog::setQuery(const QString& q) { m_query->setText(q); }

int NoteSearchDialog::resultCount() const { return m_results->count(); }

void NoteSearchDialog::onQueryChanged(const QString& text)
{
    m_results->clear();
    if (text.trimmed().isEmpty())
        return;

    // Project names give each hit its context ("title — project").
    QHash<Id, QString> projectNames;
    for (const Project& p : m_store.listProjects(true))
        projectNames.insert(p.id, p.name);

    for (const Note& n : m_store.searchNotes(text)) {
        const QString title =
            n.title.isEmpty() ? QStringLiteral("(untitled)") : n.title;
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  —  %2")
                .arg(title, projectNames.value(n.projectId,
                                               QStringLiteral("(unknown)"))));
        item->setData(kIdRole, static_cast<qlonglong>(n.id));
        m_results->addItem(item);
    }
    if (m_results->count() > 0)
        m_results->setCurrentRow(0);
}

void NoteSearchDialog::onActivated()
{
    QListWidgetItem* item = m_results->currentItem();
    if (!item && m_results->count() > 0)
        item = m_results->item(0);
    if (!item)
        return;
    m_chosen = item->data(kIdRole).toLongLong();
    accept();
}

} // namespace zb
