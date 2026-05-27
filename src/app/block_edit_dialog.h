#pragma once

// Shown on double-clicking a calendar block: edit its title/project and
// reconcile it (status + actual times + carry-over), or delete it. For a block
// that belongs to a recurring series, delete offers "this occurrence" vs
// "entire series". The reconcile controls are the shared ReconcilePanel.

#include <QDialog>

#include "storage/types.h"

#include <QDate>
#include <QDateTime>
#include <optional>

class QComboBox;
class QLineEdit;

namespace zb {

class Store;
class ReconcilePanel;

class BlockEditDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action { Save, DeleteOccurrence, DeleteSeries };

    BlockEditDialog(Store& store, bool partOfSeries, QWidget* parent = nullptr);

    // Planned span is used to default the actual times and to anchor the date
    // the actual time-of-day edits combine with.
    void setInitial(const QString& title, std::optional<Id> project,
                    const QDateTime& plannedStart, const QDateTime& plannedEnd,
                    BlockStatus status,
                    std::optional<QDateTime> actualStart,
                    std::optional<QDateTime> actualEnd);

    QString           title() const;
    std::optional<Id> projectId() const;
    Action            action() const { return m_action; }

    BlockStatus status() const;
    QDateTime   actualStart() const;
    QDateTime   actualEnd() const;
    QDate       carryTarget() const;

private slots:
    void onDelete();

private:
    QLineEdit*      m_title;
    QComboBox*      m_project;
    ReconcilePanel* m_reconcile;
    bool            m_partOfSeries;
    Action          m_action = Action::Save;
};

} // namespace zb
