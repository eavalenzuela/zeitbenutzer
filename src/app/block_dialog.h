#pragma once

// Dialog shown when creating a calendar block: title, project attribution, and
// a repeat rule. The repeat choice is mapped to an RRULE by the caller (which
// knows the anchor day) — the dialog just reports the chosen kind.

#include <QDialog>

#include "storage/types.h"

#include <optional>

class QComboBox;
class QLineEdit;

class QComboBox;

namespace zb {

class Store;

// Populate a combo with "(no project)" (data -1) followed by all projects in
// hierarchical (indented) order; each item's data is the project id.
void fillProjectCombo(QComboBox* combo, Store& store);

class BlockDialog : public QDialog {
    Q_OBJECT
public:
    enum class Repeat { None, Daily, Weekdays, Weekly, Monthly };

    explicit BlockDialog(Store& store, QWidget* parent = nullptr);

    void setInitial(const QString& title, Id project);

    QString           title() const;
    std::optional<Id> projectId() const;
    Repeat            repeat() const;

private:
    QLineEdit* m_title;
    QComboBox* m_project;   // each item's data = project id (-1 == none)
    QComboBox* m_repeat;    // index maps to Repeat
};

} // namespace zb
