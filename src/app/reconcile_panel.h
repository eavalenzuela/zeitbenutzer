#pragma once

// The reconcile controls shared by the single-block edit dialog and the guided
// evening-review walk: a status choice (Planned / Done / Skipped / Carried),
// actual-time edits (live only when Done), and a carry-to date (live only when
// Carried). Plus free helpers that apply a chosen outcome to the store.

#include <QWidget>

#include "storage/types.h"

#include <QDate>
#include <QDateTime>
#include <optional>

class QComboBox;
class QDateEdit;
class QPushButton;
class QTimeEdit;

namespace zb {

class Store;

class ReconcilePanel : public QWidget {
    Q_OBJECT
public:
    explicit ReconcilePanel(QWidget* parent = nullptr);

    // Seed from a block's planned span + current reconcile state. The planned
    // date anchors the actual time-of-day edits and defaults the carry target
    // to the next day.
    void setInitial(const QDateTime& plannedStart, const QDateTime& plannedEnd,
                    BlockStatus status, std::optional<QDateTime> actualStart,
                    std::optional<QDateTime> actualEnd);

    BlockStatus status() const;
    QDateTime   actualStart() const; // combined with the planned date
    QDateTime   actualEnd() const;
    QDate       carryTarget() const;

signals:
    void statusChanged();

private slots:
    void onStatusChanged();
    void onSameAsPlanned();

private:
    QComboBox*   m_status;
    QTimeEdit*   m_actStart;
    QTimeEdit*   m_actEnd;
    QPushButton* m_samePlanned;
    QDateEdit*   m_carryTarget;
    QDate        m_date;                        // planned date
    QTime        m_plannedStartT, m_plannedEndT; // for the "= planned" reset
};

// Apply a reconcile outcome to an already-materialized block.
void applyOutcome(Store& store, Id blockId, BlockStatus status,
                  const QDateTime& actualStart, const QDateTime& actualEnd,
                  const QDate& carryTarget);

// Resolve an occurrence to a real block (materializing a phantom when the
// outcome needs it; a phantom left Planned stays phantom) and apply the
// outcome. Returns the affected block id, or -1 if nothing was persisted.
Id applyReconcile(Store& store, const Occurrence& occ, BlockStatus status,
                  const QDateTime& actualStart, const QDateTime& actualEnd,
                  const QDate& carryTarget);

} // namespace zb
