#pragma once

// Module 5: per-project time rollup — the inverse of the calendar. Shows how
// much time is planned vs. actually spent on the selected project, this week
// vs. all time, optionally recursing sub-projects. The bidirectional payoff:
// a block says "what am I doing"; this says "where has my time gone".

#include <QWidget>

#include "storage/types.h"

class QCheckBox;
class QLabel;

namespace zb {

class Store;

class TimeRollupPanel : public QWidget {
    Q_OBJECT
public:
    explicit TimeRollupPanel(Store& store, QWidget* parent = nullptr);

    // Last computed values (for tests).
    qint64 weekPlanned() const { return m_vWeekPlanned; }
    qint64 weekActual() const { return m_vWeekActual; }

public slots:
    void setProject(zb::Id projectId); // -1 clears
    void recompute();

protected:
    void showEvent(QShowEvent* e) override; // refresh when the tab is shown

private:
    Store&     m_store;
    Id         m_projectId = -1;
    QLabel*    m_title;
    QCheckBox* m_inclSub;
    QLabel*    m_weekPlanned;
    QLabel*    m_weekActual;
    QLabel*    m_allPlanned;
    QLabel*    m_allActual;

    qint64 m_vWeekPlanned = 0;
    qint64 m_vWeekActual = 0;
};

} // namespace zb
