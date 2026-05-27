#include "app/block_edit_dialog.h"

#include "app/block_dialog.h" // fillProjectCombo
#include "app/reconcile_panel.h"
#include "storage/store.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>

namespace zb {

BlockEditDialog::BlockEditDialog(Store& store, bool partOfSeries, QWidget* parent)
    : QDialog(parent), m_partOfSeries(partOfSeries)
{
    setWindowTitle(QStringLiteral("Edit block"));

    m_title = new QLineEdit(this);
    m_project = new QComboBox(this);
    fillProjectCombo(m_project, store);
    m_reconcile = new ReconcilePanel(this);

    auto* form = new QFormLayout(this);
    form->addRow(QStringLiteral("Title"), m_title);
    form->addRow(QStringLiteral("Project"), m_project);
    form->addRow(m_reconcile);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    auto* del = buttons->addButton(QStringLiteral("Delete…"),
                                   QDialogButtonBox::DestructiveRole);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        m_action = Action::Save;
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(del, &QPushButton::clicked, this, &BlockEditDialog::onDelete);
    form->addRow(buttons);

    m_title->setFocus();
}

void BlockEditDialog::setInitial(const QString& title, std::optional<Id> project,
                                 const QDateTime& plannedStart,
                                 const QDateTime& plannedEnd, BlockStatus status,
                                 std::optional<QDateTime> actualStart,
                                 std::optional<QDateTime> actualEnd)
{
    m_title->setText(title);
    const int idx =
        m_project->findData(static_cast<qlonglong>(project ? *project : -1));
    m_project->setCurrentIndex(idx >= 0 ? idx : 0);

    m_reconcile->setInitial(plannedStart, plannedEnd, status, actualStart,
                            actualEnd);
}

BlockStatus BlockEditDialog::status() const { return m_reconcile->status(); }
QDateTime BlockEditDialog::actualStart() const { return m_reconcile->actualStart(); }
QDateTime BlockEditDialog::actualEnd() const { return m_reconcile->actualEnd(); }
QDate BlockEditDialog::carryTarget() const { return m_reconcile->carryTarget(); }

QString BlockEditDialog::title() const { return m_title->text().trimmed(); }

std::optional<Id> BlockEditDialog::projectId() const
{
    const Id id = m_project->currentData().toLongLong();
    if (id <= 0)
        return std::nullopt;
    return id;
}

void BlockEditDialog::onDelete()
{
    if (m_partOfSeries) {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("Delete recurring block"));
        box.setText(QStringLiteral("This block repeats."));
        box.setInformativeText(
            QStringLiteral("Delete only this occurrence, or the entire series?"));
        QPushButton* occ = box.addButton(QStringLiteral("This occurrence"),
                                         QMessageBox::DestructiveRole);
        QPushButton* ser = box.addButton(QStringLiteral("Entire series"),
                                         QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == occ) {
            m_action = Action::DeleteOccurrence;
            accept();
        } else if (box.clickedButton() == ser) {
            m_action = Action::DeleteSeries;
            accept();
        }
        // Cancel: stay in the edit dialog.
    } else {
        if (QMessageBox::question(this, QStringLiteral("Delete block"),
                                  QStringLiteral("Delete this block?"))
            == QMessageBox::Yes) {
            m_action = Action::DeleteOccurrence;
            accept();
        }
    }
}

} // namespace zb
