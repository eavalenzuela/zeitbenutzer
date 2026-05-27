#pragma once

// Shown when adopting a read-only external calendar event into a real,
// trackable block: pick the project to attribute it to (title and time span
// come from the event). Module 6.

#include <QDialog>

#include "storage/types.h"

#include <optional>

class QComboBox;
class QLineEdit;

namespace zb {

class Store;

class AdoptDialog : public QDialog {
    Q_OBJECT
public:
    AdoptDialog(Store& store, const QString& summary, QWidget* parent = nullptr);

    QString           title() const;
    std::optional<Id> projectId() const;

private:
    QLineEdit* m_title;
    QComboBox* m_project;
};

} // namespace zb
