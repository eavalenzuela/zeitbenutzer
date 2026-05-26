#include "app/task_list_panel.h"

#include "storage/store.h"

#include <QComboBox>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QToolBar>
#include <QVBoxLayout>

namespace zb {

namespace {
constexpr int kIdRole  = Qt::UserRole + 1;
constexpr int kEstRole = Qt::UserRole + 2;

// Combo index maps 1:1 onto TaskStatus (Todo=0, Doing=1, Done=2, Cancelled=3).
TaskStatus statusFromIndex(int i) { return static_cast<TaskStatus>(i); }
int indexFromStatus(TaskStatus s) { return static_cast<int>(s); }
} // namespace

TaskListPanel::TaskListPanel(Store& store, QWidget* parent)
    : QWidget(parent), m_store(store)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* bar = new QToolBar(this);
    bar->addAction(QStringLiteral("New task"), this, &TaskListPanel::onNewTask);
    bar->addAction(QStringLiteral("Delete task"), this,
                   &TaskListPanel::onDeleteTask);
    layout->addWidget(bar);

    m_list = new QListWidget(this);
    layout->addWidget(m_list, 1);

    // Detail row for the selected task: status + estimate.
    auto* detail = new QHBoxLayout;
    m_status = new QComboBox(this);
    m_status->addItems({QStringLiteral("Todo"), QStringLiteral("Doing"),
                        QStringLiteral("Done"), QStringLiteral("Cancelled")});
    m_estimate = new QSpinBox(this);
    m_estimate->setRange(0, 100000);
    m_estimate->setSingleStep(15);
    m_estimate->setSuffix(QStringLiteral(" min"));
    detail->addWidget(new QLabel(QStringLiteral("Status:"), this));
    detail->addWidget(m_status);
    detail->addSpacing(8);
    detail->addWidget(new QLabel(QStringLiteral("Est:"), this));
    detail->addWidget(m_estimate);
    detail->addStretch(1);
    layout->addLayout(detail);

    connect(m_list, &QListWidget::itemChanged, this,
            &TaskListPanel::onItemChanged);
    connect(m_list, &QListWidget::currentItemChanged, this,
            &TaskListPanel::onCurrentChanged);
    connect(m_status, &QComboBox::currentIndexChanged, this,
            &TaskListPanel::onStatusChanged);
    connect(m_estimate, &QSpinBox::editingFinished, this,
            &TaskListPanel::onEstimateChanged);

    setDetailEnabled(false);
}

int TaskListPanel::taskCount() const { return m_list->count(); }

void TaskListPanel::setProject(Id projectId)
{
    m_projectId = projectId;
    reload();
}

void TaskListPanel::styleItem(QListWidgetItem* item, TaskStatus status)
{
    QFont f = item->font();
    const bool struck = (status == TaskStatus::Done
                         || status == TaskStatus::Cancelled);
    f.setStrikeOut(struck);
    item->setFont(f);
    item->setForeground(status == TaskStatus::Cancelled ? QColor(0x6b, 0x72, 0x80)
                                                         : QColor());
}

void TaskListPanel::reload()
{
    m_loading = true;
    m_list->clear();
    if (m_projectId > 0) {
        for (const Task& t : m_store.tasksForProject(m_projectId)) {
            auto* item = new QListWidgetItem(t.title);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            item->setData(kIdRole, static_cast<qlonglong>(t.id));
            item->setData(kEstRole, t.estimateMin);
            styleItem(item, t.status);
            m_list->addItem(item);
        }
    }
    m_loading = false;
    setDetailEnabled(false);
}

Id TaskListPanel::selectedTaskId() const
{
    QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(kIdRole).toLongLong() : -1;
}

int TaskListPanel::selectedEstimate() const
{
    QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(kEstRole).toInt() : 0;
}

void TaskListPanel::setDetailEnabled(bool on)
{
    m_status->setEnabled(on);
    m_estimate->setEnabled(on);
}

void TaskListPanel::onNewTask()
{
    if (m_projectId <= 0)
        return;
    Task t;
    t.projectId = m_projectId;
    t.title = QStringLiteral("New task");
    const Id id = m_store.createTask(t);
    reload();
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(kIdRole).toLongLong() == id) {
            m_list->setCurrentRow(i);
            m_list->editItem(m_list->item(i)); // jump straight into renaming
            break;
        }
    }
}

void TaskListPanel::onDeleteTask()
{
    const Id id = selectedTaskId();
    if (id <= 0)
        return;
    m_store.deleteTask(id);
    reload();
}

void TaskListPanel::onItemChanged(QListWidgetItem* item)
{
    if (m_loading || !item)
        return;
    const Id id = item->data(kIdRole).toLongLong();
    if (id > 0)
        m_store.updateTask(id, item->text(), item->data(kEstRole).toInt());
}

void TaskListPanel::onCurrentChanged()
{
    const Id id = selectedTaskId();
    if (id <= 0) {
        setDetailEnabled(false);
        return;
    }
    // Populate the detail row without triggering writes back.
    m_loading = true;
    // Status isn't carried in the item; re-read from the store for accuracy.
    for (const Task& t : m_store.tasksForProject(m_projectId)) {
        if (t.id == id) {
            m_status->setCurrentIndex(indexFromStatus(t.status));
            m_estimate->setValue(t.estimateMin);
            break;
        }
    }
    m_loading = false;
    setDetailEnabled(true);
}

void TaskListPanel::onStatusChanged(int index)
{
    if (m_loading)
        return;
    const Id id = selectedTaskId();
    if (id <= 0)
        return;
    const TaskStatus status = statusFromIndex(index);
    m_store.setTaskStatus(id, status, QDateTime::currentDateTimeUtc());
    if (QListWidgetItem* item = m_list->currentItem())
        styleItem(item, status);
}

void TaskListPanel::onEstimateChanged()
{
    if (m_loading)
        return;
    QListWidgetItem* item = m_list->currentItem();
    if (!item)
        return;
    const Id id = item->data(kIdRole).toLongLong();
    if (id <= 0)
        return;
    item->setData(kEstRole, m_estimate->value());
    m_store.updateTask(id, item->text(), m_estimate->value());
}

} // namespace zb
