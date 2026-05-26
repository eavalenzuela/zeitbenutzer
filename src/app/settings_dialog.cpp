#include "app/settings_dialog.h"

#include "app/settings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>

namespace zb {

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Settings"));

    m_weekStart = new QComboBox(this);
    m_weekStart->addItem(QStringLiteral("Monday"), 1);
    m_weekStart->addItem(QStringLiteral("Sunday"), 7);
    m_weekStart->setCurrentIndex(
        Settings::instance().weekStartDay() == 7 ? 1 : 0);

    m_snap = new QComboBox(this);
    m_snap->addItem(QStringLiteral("15 minutes"), 15);
    m_snap->addItem(QStringLiteral("30 minutes"), 30);
    m_snap->setCurrentIndex(Settings::instance().snapMinutes() == 30 ? 1 : 0);

    auto* dataPath = new QLabel(Settings::instance().dataLocation(), this);
    dataPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dataPath->setStyleSheet(QStringLiteral("color: #6b7280;"));

    auto* form = new QFormLayout(this);
    form->addRow(QStringLiteral("Week starts on"), m_weekStart);
    form->addRow(QStringLiteral("Calendar snap"), m_snap);
    form->addRow(QStringLiteral("Data location"), dataPath);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        save();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

void SettingsDialog::save()
{
    Settings::instance().setWeekStartDay(m_weekStart->currentData().toInt());
    Settings::instance().setSnapMinutes(m_snap->currentData().toInt());
}

} // namespace zb
