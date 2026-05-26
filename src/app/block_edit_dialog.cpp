#include "app/block_edit_dialog.h"

#include "app/block_dialog.h" // fillProjectCombo
#include "storage/store.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTimeEdit>

namespace zb {

BlockEditDialog::BlockEditDialog(Store& store, bool partOfSeries, QWidget* parent)
    : QDialog(parent), m_partOfSeries(partOfSeries)
{
    setWindowTitle(QStringLiteral("Edit block"));

    m_title = new QLineEdit(this);
    m_project = new QComboBox(this);
    fillProjectCombo(m_project, store);

    // Reconcile controls.
    m_status = new QComboBox(this);
    m_status->addItems({QStringLiteral("Planned"), QStringLiteral("Done"),
                        QStringLiteral("Skipped")});
    m_actStart = new QTimeEdit(this);
    m_actEnd = new QTimeEdit(this);
    m_actStart->setDisplayFormat(QStringLiteral("HH:mm"));
    m_actEnd->setDisplayFormat(QStringLiteral("HH:mm"));
    m_samePlanned = new QPushButton(QStringLiteral("= planned"), this);

    auto* actualRow = new QHBoxLayout;
    actualRow->addWidget(m_actStart);
    actualRow->addWidget(new QLabel(QStringLiteral("–"), this));
    actualRow->addWidget(m_actEnd);
    actualRow->addWidget(m_samePlanned);
    actualRow->addStretch(1);

    auto* form = new QFormLayout(this);
    form->addRow(QStringLiteral("Title"), m_title);
    form->addRow(QStringLiteral("Project"), m_project);
    form->addRow(QStringLiteral("Status"), m_status);
    form->addRow(QStringLiteral("Actual"), actualRow);

    connect(m_status, &QComboBox::currentIndexChanged, this,
            &BlockEditDialog::onStatusChanged);
    connect(m_samePlanned, &QPushButton::clicked, this,
            &BlockEditDialog::onSameAsPlanned);

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

    m_date = plannedStart.date();
    m_plannedStartT = plannedStart.time();
    m_plannedEndT = plannedEnd.time();

    // Map BlockStatus to the 3-item combo (Carried isn't offered here).
    int sIdx = 0;
    if (status == BlockStatus::Done)
        sIdx = 1;
    else if (status == BlockStatus::Skipped)
        sIdx = 2;
    m_status->setCurrentIndex(sIdx);

    // Default the actual times to the recorded actuals, else the planned span.
    m_actStart->setTime((actualStart && actualStart->isValid())
                            ? actualStart->time()
                            : plannedStart.time());
    m_actEnd->setTime((actualEnd && actualEnd->isValid()) ? actualEnd->time()
                                                          : plannedEnd.time());
    onStatusChanged();
}

void BlockEditDialog::onStatusChanged()
{
    const bool done = (m_status->currentIndex() == 1);
    m_actStart->setEnabled(done);
    m_actEnd->setEnabled(done);
    m_samePlanned->setEnabled(done);
}

void BlockEditDialog::onSameAsPlanned()
{
    m_actStart->setTime(m_plannedStartT);
    m_actEnd->setTime(m_plannedEndT);
}

BlockStatus BlockEditDialog::status() const
{
    switch (m_status->currentIndex()) {
    case 1: return BlockStatus::Done;
    case 2: return BlockStatus::Skipped;
    default: return BlockStatus::Planned;
    }
}

QDateTime BlockEditDialog::actualStart() const
{
    return QDateTime(m_date, m_actStart->time());
}

QDateTime BlockEditDialog::actualEnd() const
{
    return QDateTime(m_date, m_actEnd->time());
}

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
