#pragma once

// Small modal settings page. Writes straight to Settings on accept.

#include <QDialog>

class QComboBox;

namespace zb {

class Store;

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(Store& store, QWidget* parent = nullptr);

private:
    void save();

    Store&     m_store;
    QComboBox* m_theme;
    QComboBox* m_weekStart;
    QComboBox* m_snap;
    QComboBox* m_imageBackend;
};

} // namespace zb
