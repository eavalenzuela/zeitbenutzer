#pragma once

// "Schedule…" for a task: pick a date, start time, and duration, and the task
// list turns it into a real calendar block linked to the task (block_task).
// The duration prefills from the task's estimate — the design's task-pool →
// calendar loop.

#include <QDateTime>
#include <QDialog>

class QDateEdit;
class QSpinBox;
class QTimeEdit;

namespace zb {

class ScheduleTaskDialog : public QDialog {
    Q_OBJECT
public:
    // `estimateMin` prefills the duration (0 → 60 min default).
    explicit ScheduleTaskDialog(const QString& taskTitle, int estimateMin,
                                QWidget* parent = nullptr);

    QDateTime start() const;
    QDateTime end() const;
    int durationMinutes() const; // for tests

private:
    QDateEdit* m_date;
    QTimeEdit* m_time;
    QSpinBox*  m_duration;
};

} // namespace zb
