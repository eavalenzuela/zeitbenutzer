#include "app/schedule_task_dialog.h"

#include <QDateEdit>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QTimeEdit>

namespace zb {

ScheduleTaskDialog::ScheduleTaskDialog(const QString& taskTitle, int estimateMin,
                                       QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Schedule “%1”").arg(taskTitle));

    m_date = new QDateEdit(QDate::currentDate(), this);
    m_date->setDisplayFormat(QStringLiteral("ddd d MMM yyyy"));
    m_date->setCalendarPopup(true);

    // Default start: the next full hour (23:00 at the latest, so the block
    // still fits inside the chosen day).
    const int nextHour = qMin(23, QTime::currentTime().hour() + 1);
    m_time = new QTimeEdit(QTime(nextHour, 0), this);
    m_time->setDisplayFormat(QStringLiteral("HH:mm"));

    m_duration = new QSpinBox(this);
    m_duration->setRange(15, 24 * 60);
    m_duration->setSingleStep(15);
    m_duration->setSuffix(QStringLiteral(" min"));
    m_duration->setValue(estimateMin > 0 ? estimateMin : 60);

    auto* form = new QFormLayout(this);
    form->addRow(QStringLiteral("Date"), m_date);
    form->addRow(QStringLiteral("Start"), m_time);
    form->addRow(QStringLiteral("Duration"), m_duration);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

QDateTime ScheduleTaskDialog::start() const
{
    return QDateTime(m_date->date(), m_time->time());
}

QDateTime ScheduleTaskDialog::end() const
{
    return start().addSecs(qint64(m_duration->value()) * 60);
}

int ScheduleTaskDialog::durationMinutes() const { return m_duration->value(); }

} // namespace zb
