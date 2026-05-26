#pragma once

// Shown on double-clicking a calendar block: edit its title/project, or delete
// it. For a block that belongs to a recurring series, delete offers "this
// occurrence" vs "entire series".

#include <QDialog>

#include "storage/types.h"

#include <QDateTime>
#include <optional>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTimeEdit;

namespace zb {

class Store;

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
    QDateTime   actualStart() const; // combined with the planned date
    QDateTime   actualEnd() const;

private slots:
    void onDelete();
    void onStatusChanged();
    void onSameAsPlanned();

private:
    QLineEdit*  m_title;
    QComboBox*  m_project;
    QComboBox*  m_status;
    QTimeEdit*  m_actStart;
    QTimeEdit*  m_actEnd;
    QPushButton* m_samePlanned;
    QDate       m_date;        // planned date, for combining actual times
    QTime       m_plannedStartT, m_plannedEndT; // for the "= planned" reset
    bool        m_partOfSeries;
    Action      m_action = Action::Save;
};

} // namespace zb
