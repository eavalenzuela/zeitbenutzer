#pragma once

// Small modal settings page. Writes straight to Settings on accept.

#include <QDialog>

class QComboBox;

namespace zb {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    void save();

    QComboBox* m_weekStart;
    QComboBox* m_snap;
};

} // namespace zb
