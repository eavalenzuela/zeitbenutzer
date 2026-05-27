#include "app/settings_dialog.h"

#include "app/markdown_image.h"
#include "app/settings.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>

namespace zb {

SettingsDialog::SettingsDialog(Store& store, QWidget* parent)
    : QDialog(parent), m_store(store)
{
    setWindowTitle(QStringLiteral("Settings"));

    m_theme = new QComboBox(this);
    m_theme->addItem(QStringLiteral("Light"), QStringLiteral("light"));
    m_theme->addItem(QStringLiteral("Dark"), QStringLiteral("dark"));
    m_theme->setCurrentIndex(
        Settings::instance().themeName() == QStringLiteral("dark") ? 1 : 0);

    m_weekStart = new QComboBox(this);
    m_weekStart->addItem(QStringLiteral("Monday"), 1);
    m_weekStart->addItem(QStringLiteral("Sunday"), 7);
    m_weekStart->setCurrentIndex(
        Settings::instance().weekStartDay() == 7 ? 1 : 0);

    m_snap = new QComboBox(this);
    m_snap->addItem(QStringLiteral("15 minutes"), 15);
    m_snap->addItem(QStringLiteral("30 minutes"), 30);
    m_snap->setCurrentIndex(Settings::instance().snapMinutes() == 30 ? 1 : 0);

    m_imageBackend = new QComboBox(this);
    m_imageBackend->addItem(QStringLiteral("In the database"), QStringLiteral("blob"));
    m_imageBackend->addItem(QStringLiteral("Files beside the database"),
                            QStringLiteral("disk"));
    m_imageBackend->setCurrentIndex(
        Settings::instance().imageStorageBackend() == QStringLiteral("disk") ? 1 : 0);

    auto* dataPath = new QLabel(Settings::instance().dataLocation(), this);
    dataPath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dataPath->setStyleSheet(QStringLiteral("color: #6b7280;"));

    auto* form = new QFormLayout(this);
    form->addRow(QStringLiteral("Theme"), m_theme);
    form->addRow(QStringLiteral("Week starts on"), m_weekStart);
    form->addRow(QStringLiteral("Calendar snap"), m_snap);
    form->addRow(QStringLiteral("Image storage"), m_imageBackend);
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
    Settings::instance().setThemeName(m_theme->currentData().toString());
    Settings::instance().setWeekStartDay(m_weekStart->currentData().toInt());
    Settings::instance().setSnapMinutes(m_snap->currentData().toInt());

    // If the image backend changed, migrate existing images to match. Content
    // is unchanged, so rendered images stay valid (no cache invalidation).
    const QString backend = m_imageBackend->currentData().toString();
    if (backend != Settings::instance().imageStorageBackend()) {
        Settings::instance().setImageStorageBackend(backend);
        migrateImageStorage(m_store, backend == QStringLiteral("disk"));
    }
}

} // namespace zb
