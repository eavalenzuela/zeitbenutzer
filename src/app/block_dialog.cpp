#include "app/block_dialog.h"

#include "storage/store.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QHash>
#include <QLineEdit>

namespace zb {

namespace {
constexpr int kIdRole = Qt::UserRole + 1;
} // namespace

void fillProjectCombo(QComboBox* combo, Store& store)
{
    combo->addItem(QStringLiteral("(no project)"), static_cast<qlonglong>(-1));

    // List projects hierarchically (DFS from roots, indented by depth).
    const QList<Project> projects = store.listProjects(false);
    QHash<Id, QList<Project>> children;
    QList<Project> roots;
    for (const Project& p : projects) {
        if (p.parentId)
            children[*p.parentId].append(p);
        else
            roots.append(p);
    }
    struct Frame { Project p; int depth; };
    QList<Frame> stack;
    for (auto it = roots.crbegin(); it != roots.crend(); ++it)
        stack.append({*it, 0});
    while (!stack.isEmpty()) {
        const Frame f = stack.takeLast();
        combo->addItem(QString(f.depth * 4, QLatin1Char(' ')) + f.p.name,
                       static_cast<qlonglong>(f.p.id));
        const QList<Project> kids = children.value(f.p.id);
        for (auto it = kids.crbegin(); it != kids.crend(); ++it)
            stack.append({*it, f.depth + 1});
    }
}

BlockDialog::BlockDialog(Store& store, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("New block"));

    m_title = new QLineEdit(this);
    m_title->setPlaceholderText(QStringLiteral("Block title"));

    m_project = new QComboBox(this);
    fillProjectCombo(m_project, store);

    m_repeat = new QComboBox(this);
    m_repeat->addItems({QStringLiteral("Does not repeat"),
                        QStringLiteral("Daily"),
                        QStringLiteral("Weekdays (Mon–Fri)"),
                        QStringLiteral("Weekly"),
                        QStringLiteral("Monthly")});

    m_count = new QSpinBox(this);
    m_count->setRange(0, 999);
    m_count->setSpecialValueText(QStringLiteral("∞ (forever)"));
    m_count->setSuffix(QStringLiteral(" times"));
    m_count->setEnabled(false); // only meaningful once a repeat is chosen

    connect(m_repeat, &QComboBox::currentIndexChanged, this,
            [this](int i) { m_count->setEnabled(i != 0); });

    auto* form = new QFormLayout(this);
    form->addRow(QStringLiteral("Title"), m_title);
    form->addRow(QStringLiteral("Project"), m_project);
    form->addRow(QStringLiteral("Repeat"), m_repeat);
    form->addRow(QStringLiteral("Repeat for"), m_count);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);

    m_title->setFocus();
}

void BlockDialog::setInitial(const QString& title, Id project)
{
    m_title->setText(title);
    const int idx = m_project->findData(static_cast<qlonglong>(project));
    m_project->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString BlockDialog::title() const { return m_title->text().trimmed(); }

std::optional<Id> BlockDialog::projectId() const
{
    const Id id = m_project->currentData().toLongLong();
    if (id <= 0)
        return std::nullopt;
    return id;
}

BlockDialog::Repeat BlockDialog::repeat() const
{
    return static_cast<Repeat>(m_repeat->currentIndex());
}

int BlockDialog::count() const { return m_count->value(); }

} // namespace zb
