#pragma once

// The guided evening-review walk: steps through a queue of unreconciled
// occurrences one at a time, offering the shared reconcile controls (done /
// skipped / carried) plus Back / Skip navigation and a progress indicator.
// Each Apply commits that block's outcome to the store immediately.

#include <QDialog>
#include <QHash>
#include <QList>

#include "storage/types.h"

class QLabel;
class QPushButton;

namespace zb {

class Store;
class ReconcilePanel;

class DayReviewDialog : public QDialog {
    Q_OBJECT
public:
    DayReviewDialog(Store& store, QList<Occurrence> queue,
                    QWidget* parent = nullptr);

private:
    void seedCurrent();   // load occurrence m_index into the controls
    void updateButtons();
    void applyAndAdvance();
    void skip();
    void back();

    Store&            m_store;
    QList<Occurrence> m_queue;
    QHash<Id, QString> m_projectNames;
    int               m_index = 0;

    QLabel*         m_progress;
    QLabel*         m_heading;
    QLabel*         m_meta;
    ReconcilePanel* m_reconcile;
    QPushButton*    m_back;
    QPushButton*    m_skip;
    QPushButton*    m_apply;
};

} // namespace zb
