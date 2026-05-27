#include "app/adopt_dialog.h"

#include "app/block_dialog.h" // fillProjectCombo
#include "storage/store.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>

namespace zb {

AdoptDialog::AdoptDialog(Store& store, const QString& summary, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Adopt into block"));

    m_title = new QLineEdit(summary, this);
    m_project = new QComboBox(this);
    fillProjectCombo(m_project, store);

    auto* form = new QFormLayout(this);
    form->addRow(QStringLiteral("Title"), m_title);
    form->addRow(QStringLiteral("Project"), m_project);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);

    m_title->setFocus();
    m_title->selectAll();
}

QString AdoptDialog::title() const { return m_title->text().trimmed(); }

std::optional<Id> AdoptDialog::projectId() const
{
    const Id id = m_project->currentData().toLongLong();
    if (id <= 0)
        return std::nullopt;
    return id;
}

} // namespace zb
