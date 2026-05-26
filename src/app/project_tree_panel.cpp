#include "app/project_tree_panel.h"

#include "app/theme.h"
#include "storage/store.h"

#include <QColorDialog>
#include <QHash>
#include <QIcon>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QToolBar>
#include <QTreeView>
#include <QVBoxLayout>

namespace {
QIcon colorSwatch(const QColor& c)
{
    QPixmap pm(12, 12);
    pm.fill(Qt::transparent);
    QPainter pt(&pm);
    pt.setRenderHint(QPainter::Antialiasing, true);
    pt.setBrush(c);
    pt.setPen(Qt::NoPen);
    pt.drawRoundedRect(0, 0, 12, 12, 3, 3);
    return QIcon(pm);
}
} // namespace

namespace zb {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
}

ProjectTreePanel::ProjectTreePanel(Store& store, QWidget* parent)
    : QWidget(parent), m_store(store)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* bar = new QToolBar(this);
    bar->addAction(QStringLiteral("New"), this, &ProjectTreePanel::onNewTopProject);
    bar->addAction(QStringLiteral("New child"), this,
                   &ProjectTreePanel::onNewChildProject);
    bar->addAction(QStringLiteral("Delete"), this,
                   &ProjectTreePanel::onDeleteProject);
    layout->addWidget(bar);

    m_model = new QStandardItemModel(this);
    m_view = new QTreeView(this);
    m_view->setModel(m_model);
    m_view->setHeaderHidden(true);
    m_view->setEditTriggers(QAbstractItemView::DoubleClicked
                            | QAbstractItemView::EditKeyPressed);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_view);

    connect(m_view, &QTreeView::customContextMenuRequested, this,
            &ProjectTreePanel::onContextMenu);
    connect(m_model, &QStandardItemModel::itemChanged, this,
            &ProjectTreePanel::onItemChanged);
    connect(m_view->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &ProjectTreePanel::onCurrentChanged);

    reload();
}

void ProjectTreePanel::reload()
{
    const Id keep = selectedProjectId();

    m_reloading = true;
    m_model->clear();

    const QList<Project> projects = m_store.listProjects(false);
    m_count = projects.size();

    // Two passes so a child can attach to a parent regardless of list order.
    QHash<Id, QStandardItem*> items;
    for (const Project& p : projects) {
        auto* it = new QStandardItem(p.name);
        it->setData(static_cast<qlonglong>(p.id), kIdRole);
        it->setIcon(colorSwatch(projectColor(p.color, p.id)));
        it->setEditable(true);
        items.insert(p.id, it);
    }
    for (const Project& p : projects) {
        QStandardItem* it = items.value(p.id);
        if (p.parentId && items.contains(*p.parentId))
            items.value(*p.parentId)->appendRow(it);
        else
            m_model->invisibleRootItem()->appendRow(it);
    }
    m_view->expandAll();
    m_reloading = false;

    if (keep > 0)
        selectProjectById(keep);
}

Id ProjectTreePanel::selectedProjectId() const
{
    const QModelIndex idx = m_view->currentIndex();
    if (!idx.isValid())
        return -1;
    return idx.data(kIdRole).toLongLong();
}

void ProjectTreePanel::selectProjectById(Id id)
{
    // Walk the whole tree looking for the matching id.
    QList<QStandardItem*> stack;
    for (int r = 0; r < m_model->rowCount(); ++r)
        stack.append(m_model->item(r));
    while (!stack.isEmpty()) {
        QStandardItem* it = stack.takeLast();
        if (it->data(kIdRole).toLongLong() == id) {
            m_view->setCurrentIndex(it->index());
            return;
        }
        for (int r = 0; r < it->rowCount(); ++r)
            stack.append(it->child(r));
    }
}

void ProjectTreePanel::onNewTopProject()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("New project"), QStringLiteral("Name:"),
        QLineEdit::Normal, QStringLiteral("New project"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    Project p;
    p.name = name.trimmed();
    const Id id = m_store.createProject(p);
    reload();
    selectProjectById(id);
    emit projectsChanged();
}

void ProjectTreePanel::onNewChildProject()
{
    const Id parent = selectedProjectId();
    if (parent <= 0) {
        QMessageBox::information(this, QStringLiteral("New child"),
                                 QStringLiteral("Select a parent project first."));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("New child project"), QStringLiteral("Name:"),
        QLineEdit::Normal, QStringLiteral("New project"), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    Project p;
    p.name = name.trimmed();
    p.parentId = parent;
    const Id id = m_store.createProject(p);
    reload();
    selectProjectById(id);
    emit projectsChanged();
}

void ProjectTreePanel::onDeleteProject()
{
    const Id id = selectedProjectId();
    if (id <= 0)
        return;
    const auto reply = QMessageBox::question(
        this, QStringLiteral("Delete project"),
        QStringLiteral("Delete this project, its sub-projects and their notes?"));
    if (reply != QMessageBox::Yes)
        return;
    m_store.deleteProject(id);
    reload();
    emit projectSelected(selectedProjectId());
    emit projectsChanged();
}

void ProjectTreePanel::onSetColor()
{
    const Id id = selectedProjectId();
    if (id <= 0)
        return;
    QColor current = Qt::white;
    for (const Project& p : m_store.listProjects(true))
        if (p.id == id)
            current = projectColor(p.color, p.id);
    const QColor c = QColorDialog::getColor(current, this,
                                            QStringLiteral("Project colour"));
    if (!c.isValid())
        return;
    m_store.setProjectColor(id, c.name());
    reload();
    emit projectsChanged();
}

void ProjectTreePanel::onContextMenu(const QPoint& pos)
{
    const QModelIndex idx = m_view->indexAt(pos);
    if (!idx.isValid())
        return;
    m_view->setCurrentIndex(idx);

    QMenu menu(this);
    menu.addAction(QStringLiteral("Set colour…"), this,
                   &ProjectTreePanel::onSetColor);
    menu.addAction(QStringLiteral("New child"), this,
                   &ProjectTreePanel::onNewChildProject);
    menu.addSeparator();
    menu.addAction(QStringLiteral("Delete"), this,
                   &ProjectTreePanel::onDeleteProject);
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}

void ProjectTreePanel::onItemChanged(QStandardItem* item)
{
    if (m_reloading || !item)
        return;
    const Id id = item->data(kIdRole).toLongLong();
    if (id > 0)
        m_store.renameProject(id, item->text());
}

void ProjectTreePanel::onCurrentChanged()
{
    emit projectSelected(selectedProjectId());
}

} // namespace zb
