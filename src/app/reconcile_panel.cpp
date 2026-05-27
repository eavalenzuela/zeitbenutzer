#include "app/reconcile_panel.h"

#include "storage/store.h"

#include <QComboBox>
#include <QDateEdit>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimeEdit>

namespace zb {

ReconcilePanel::ReconcilePanel(QWidget* parent) : QWidget(parent)
{
    m_status = new QComboBox(this);
    m_status->addItems({QStringLiteral("Planned"), QStringLiteral("Done"),
                        QStringLiteral("Skipped"), QStringLiteral("Carried")});

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

    m_carryTarget = new QDateEdit(this);
    m_carryTarget->setDisplayFormat(QStringLiteral("ddd d MMM yyyy"));
    m_carryTarget->setCalendarPopup(true);

    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    form->addRow(QStringLiteral("Status"), m_status);
    form->addRow(QStringLiteral("Actual"), actualRow);
    form->addRow(QStringLiteral("Carry to"), m_carryTarget);

    connect(m_status, &QComboBox::currentIndexChanged, this,
            &ReconcilePanel::onStatusChanged);
    connect(m_samePlanned, &QPushButton::clicked, this,
            &ReconcilePanel::onSameAsPlanned);
}

void ReconcilePanel::setInitial(const QDateTime& plannedStart,
                                const QDateTime& plannedEnd, BlockStatus status,
                                std::optional<QDateTime> actualStart,
                                std::optional<QDateTime> actualEnd)
{
    m_date = plannedStart.date();
    m_plannedStartT = plannedStart.time();
    m_plannedEndT = plannedEnd.time();

    int sIdx = 0;
    switch (status) {
    case BlockStatus::Done: sIdx = 1; break;
    case BlockStatus::Skipped: sIdx = 2; break;
    case BlockStatus::Carried: sIdx = 3; break;
    default: sIdx = 0; break;
    }
    m_status->setCurrentIndex(sIdx);

    m_actStart->setTime((actualStart && actualStart->isValid())
                            ? actualStart->time()
                            : plannedStart.time());
    m_actEnd->setTime((actualEnd && actualEnd->isValid()) ? actualEnd->time()
                                                          : plannedEnd.time());
    m_carryTarget->setDate(m_date.addDays(1));
    m_carryTarget->setMinimumDate(m_date.addDays(1));

    onStatusChanged();
}

void ReconcilePanel::onStatusChanged()
{
    const bool done = (m_status->currentIndex() == 1);
    const bool carried = (m_status->currentIndex() == 3);
    m_actStart->setEnabled(done);
    m_actEnd->setEnabled(done);
    m_samePlanned->setEnabled(done);
    m_carryTarget->setEnabled(carried);
    emit statusChanged();
}

void ReconcilePanel::onSameAsPlanned()
{
    m_actStart->setTime(m_plannedStartT);
    m_actEnd->setTime(m_plannedEndT);
}

BlockStatus ReconcilePanel::status() const
{
    switch (m_status->currentIndex()) {
    case 1: return BlockStatus::Done;
    case 2: return BlockStatus::Skipped;
    case 3: return BlockStatus::Carried;
    default: return BlockStatus::Planned;
    }
}

QDateTime ReconcilePanel::actualStart() const
{
    return QDateTime(m_date, m_actStart->time());
}

QDateTime ReconcilePanel::actualEnd() const
{
    return QDateTime(m_date, m_actEnd->time());
}

QDate ReconcilePanel::carryTarget() const { return m_carryTarget->date(); }

// --- outcome application -----------------------------------------------------

void applyOutcome(Store& store, Id blockId, BlockStatus status,
                  const QDateTime& actualStart, const QDateTime& actualEnd,
                  const QDate& carryTarget)
{
    if (blockId <= 0)
        return;
    switch (status) {
    case BlockStatus::Done:
        store.reconcileBlock(blockId, actualStart, actualEnd, BlockStatus::Done);
        break;
    case BlockStatus::Skipped:
        store.reconcileBlock(blockId, QDateTime(), QDateTime(),
                             BlockStatus::Skipped);
        break;
    case BlockStatus::Carried:
        store.carryOverBlock(blockId, carryTarget);
        break;
    default: // Planned → un-reconcile (clear actuals)
        store.reconcileBlock(blockId, QDateTime(), QDateTime(),
                             BlockStatus::Planned);
        break;
    }
}

Id applyReconcile(Store& store, const Occurrence& occ, BlockStatus status,
                  const QDateTime& actualStart, const QDateTime& actualEnd,
                  const QDate& carryTarget)
{
    // A phantom left Planned has nothing to persist — keep it a phantom.
    if (status == BlockStatus::Planned && !occ.materialized)
        return -1;

    const Id id = occ.materialized
                      ? occ.blockId
                      : (occ.seriesId ? store.materializeBlockOccurrence(
                                            *occ.seriesId, occ.occurrenceDate)
                                      : -1);
    applyOutcome(store, id, status, actualStart, actualEnd, carryTarget);
    return id;
}

} // namespace zb
