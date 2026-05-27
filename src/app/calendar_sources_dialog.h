#pragma once

// Module 6: manage external calendar sources (App → Calendars…). A list with
// add / edit / remove and a "Refresh now" button. A small SourceEditDialog
// captures one source's fields. Sources persist in the store; refreshing pulls
// their events through ExternalSync.

#include <QDialog>

#include "storage/types.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace zb {

class Store;
class ExternalSync;

// Add/edit a single source.
class SourceEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit SourceEditDialog(QWidget* parent = nullptr);

    void           setSource(const ExternalSource& s);
    ExternalSource source() const;

private slots:
    void browse();
    void pickColor();

private:
    void applyColorSwatch();

    QLineEdit*   m_name;
    QComboBox*   m_kind;
    QLineEdit*   m_location;
    QPushButton* m_browse;
    QPushButton* m_color;
    QCheckBox*   m_enabled;
    QString      m_colorHex;
    Id           m_id = -1;
};

class CalendarSourcesDialog : public QDialog {
    Q_OBJECT
public:
    CalendarSourcesDialog(Store& store, ExternalSync& sync,
                          QWidget* parent = nullptr);

private slots:
    void add();
    void edit();
    void remove();

private:
    void reloadList();
    Id   selectedId() const;

    Store&        m_store;
    ExternalSync& m_sync;
    QListWidget*  m_list;
    QPushButton*  m_edit;
    QPushButton*  m_remove;
};

} // namespace zb
