#include "app/time_rollup_panel.h"

#include "storage/store.h"

#include <QCheckBox>
#include <QDate>
#include <QGridLayout>
#include <QLabel>

namespace zb {

namespace {
QDate mondayOf(const QDate& d) { return d.addDays(-(d.dayOfWeek() - 1)); }

QString fmtMinutes(qint64 m)
{
    if (m <= 0)
        return QStringLiteral("—");
    const qint64 h = m / 60, mm = m % 60;
    if (h && mm)
        return QStringLiteral("%1h %2m").arg(h).arg(mm);
    if (h)
        return QStringLiteral("%1h").arg(h);
    return QStringLiteral("%1m").arg(mm);
}
} // namespace

TimeRollupPanel::TimeRollupPanel(Store& store, QWidget* parent)
    : QWidget(parent), m_store(store)
{
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(12, 12, 12, 12);
    grid->setHorizontalSpacing(24);
    grid->setVerticalSpacing(8);

    m_title = new QLabel(this);
    QFont tf = m_title->font();
    tf.setPointSize(tf.pointSize() + 3);
    tf.setBold(true);
    m_title->setFont(tf);
    grid->addWidget(m_title, 0, 0, 1, 3);

    m_inclSub = new QCheckBox(QStringLiteral("Include sub-projects"), this);
    m_inclSub->setChecked(true);
    grid->addWidget(m_inclSub, 1, 0, 1, 3);

    // Header row.
    auto* hp = new QLabel(QStringLiteral("Planned"), this);
    auto* ha = new QLabel(QStringLiteral("Actual"), this);
    grid->addWidget(hp, 2, 1);
    grid->addWidget(ha, 2, 2);

    auto* wk = new QLabel(QStringLiteral("This week"), this);
    auto* al = new QLabel(QStringLiteral("All time"), this);
    m_weekPlanned = new QLabel(this);
    m_weekActual = new QLabel(this);
    m_allPlanned = new QLabel(this);
    m_allActual = new QLabel(this);
    grid->addWidget(wk, 3, 0);
    grid->addWidget(m_weekPlanned, 3, 1);
    grid->addWidget(m_weekActual, 3, 2);
    grid->addWidget(al, 4, 0);
    grid->addWidget(m_allPlanned, 4, 1);
    grid->addWidget(m_allActual, 4, 2);

    grid->setRowStretch(5, 1);
    grid->setColumnStretch(3, 1);

    connect(m_inclSub, &QCheckBox::toggled, this, &TimeRollupPanel::recompute);

    recompute();
}

void TimeRollupPanel::setProject(Id projectId)
{
    m_projectId = projectId;
    recompute();
}

void TimeRollupPanel::showEvent(QShowEvent* e)
{
    QWidget::showEvent(e);
    recompute(); // reflect reconciles made elsewhere since last shown
}

void TimeRollupPanel::recompute()
{
    if (m_projectId <= 0) {
        m_title->setText(QStringLiteral("No project selected"));
        for (QLabel* l : {m_weekPlanned, m_weekActual, m_allPlanned, m_allActual})
            l->setText(QStringLiteral("—"));
        m_vWeekPlanned = m_vWeekActual = 0;
        return;
    }

    QString name = QStringLiteral("Project %1").arg(m_projectId);
    for (const Project& p : m_store.listProjects(true))
        if (p.id == m_projectId)
            name = p.name;
    m_title->setText(name);

    const bool sub = m_inclSub->isChecked();
    const QDate mon = mondayOf(QDate::currentDate());
    const QDateTime ws(mon, QTime(0, 0));
    const QDateTime we(mon.addDays(6), QTime(23, 59, 59));

    m_vWeekPlanned = m_store.plannedMinutes(m_projectId, sub, ws, we);
    m_vWeekActual = m_store.actualMinutes(m_projectId, sub, ws, we);
    const qint64 allP = m_store.plannedMinutes(m_projectId, sub);
    const qint64 allA = m_store.actualMinutes(m_projectId, sub);

    m_weekPlanned->setText(fmtMinutes(m_vWeekPlanned));
    m_weekActual->setText(fmtMinutes(m_vWeekActual));
    m_allPlanned->setText(fmtMinutes(allP));
    m_allActual->setText(fmtMinutes(allA));
}

} // namespace zb
