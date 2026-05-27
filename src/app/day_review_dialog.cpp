#include "app/day_review_dialog.h"

#include "app/reconcile_panel.h"
#include "storage/store.h"

#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace zb {

DayReviewDialog::DayReviewDialog(Store& store, QList<Occurrence> queue,
                                 QWidget* parent)
    : QDialog(parent), m_store(store), m_queue(std::move(queue))
{
    setWindowTitle(QStringLiteral("Review today"));

    for (const Project& p : m_store.listProjects(true))
        m_projectNames.insert(p.id, p.name);

    m_progress = new QLabel(this);
    m_heading = new QLabel(this);
    QFont hf = m_heading->font();
    hf.setBold(true);
    hf.setPointSizeF(hf.pointSizeF() + 2.0);
    m_heading->setFont(hf);
    m_heading->setWordWrap(true);
    m_meta = new QLabel(this);

    auto* rule = new QFrame(this);
    rule->setFrameShape(QFrame::HLine);

    m_reconcile = new ReconcilePanel(this);

    m_back = new QPushButton(QStringLiteral("◀ Back"), this);
    m_skip = new QPushButton(QStringLiteral("Skip"), this);
    m_apply = new QPushButton(this);
    m_apply->setDefault(true);
    auto* close = new QPushButton(QStringLiteral("Close"), this);

    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(m_back);
    btnRow->addWidget(m_skip);
    btnRow->addStretch(1);
    btnRow->addWidget(close);
    btnRow->addWidget(m_apply);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_progress);
    layout->addWidget(m_heading);
    layout->addWidget(m_meta);
    layout->addWidget(rule);
    layout->addWidget(m_reconcile);
    layout->addStretch(1);
    layout->addLayout(btnRow);

    connect(m_back, &QPushButton::clicked, this, &DayReviewDialog::back);
    connect(m_skip, &QPushButton::clicked, this, &DayReviewDialog::skip);
    connect(m_apply, &QPushButton::clicked, this,
            &DayReviewDialog::applyAndAdvance);
    connect(close, &QPushButton::clicked, this, &QDialog::accept);

    seedCurrent();
}

void DayReviewDialog::seedCurrent()
{
    if (m_index < 0 || m_index >= m_queue.size()) {
        accept();
        return;
    }
    const Occurrence& o = m_queue.at(m_index);

    m_progress->setText(QStringLiteral("Block %1 of %2")
                            .arg(m_index + 1)
                            .arg(m_queue.size()));
    m_heading->setText(o.title.isEmpty() ? QStringLiteral("(untitled block)")
                                         : o.title);

    const QString project =
        o.projectId ? m_projectNames.value(*o.projectId,
                                           QStringLiteral("(unknown)"))
                    : QStringLiteral("(no project)");
    m_meta->setText(QStringLiteral("%1  ·  %2–%3")
                        .arg(project,
                             o.plannedStart.toString(QStringLiteral("HH:mm")),
                             o.plannedEnd.toString(QStringLiteral("HH:mm"))));

    m_reconcile->setInitial(o.plannedStart, o.plannedEnd, o.status,
                            o.actualStart, o.actualEnd);
    updateButtons();
}

void DayReviewDialog::updateButtons()
{
    m_back->setEnabled(m_index > 0);
    const bool last = (m_index == m_queue.size() - 1);
    m_apply->setText(last ? QStringLiteral("Apply & Finish")
                          : QStringLiteral("Apply & Next ▶"));
}

void DayReviewDialog::applyAndAdvance()
{
    const Occurrence& o = m_queue.at(m_index);
    applyReconcile(m_store, o, m_reconcile->status(), m_reconcile->actualStart(),
                   m_reconcile->actualEnd(), m_reconcile->carryTarget());
    ++m_index;
    seedCurrent();
}

void DayReviewDialog::skip()
{
    ++m_index;
    seedCurrent();
}

void DayReviewDialog::back()
{
    if (m_index > 0) {
        --m_index;
        seedCurrent();
    }
}

} // namespace zb
