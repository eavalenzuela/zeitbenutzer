#include "app/main_window.h"
#include "app/settings.h"
#include "app/theme.h"
#include "app/typography.h"
#include "storage/database.h"
#include "storage/store.h"

#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QMessageBox>
#include <QStandardPaths>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("zeitbenutzer"));
    // Lets Wayland associate the window with the installed .desktop entry (and
    // thus its icon) by app_id.
    QGuiApplication::setDesktopFileName(QStringLiteral("zeitbenutzer"));

    // Window / taskbar / Alt-Tab icon. Built from the bundled multi-size PNGs so
    // it renders crisply at whatever size the WM requests, installed or not.
    QIcon appIcon;
    for (int sz : {16, 32, 48, 64, 128, 256, 512})
        appIcon.addFile(QStringLiteral(":/icons/app-%1.png").arg(sz));
    QApplication::setWindowIcon(appIcon);

    zb::registerBundledFonts();
    QApplication::setFont(zb::uiFont()); // chrome + preview; editor overrides to mono
    zb::applyTheme(app, zb::themeByName(zb::Settings::instance().themeName()));

    const QString dir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString dbPath = dir + QStringLiteral("/zeitbenutzer.db");

    zb::Database db;
    QString error;
    if (!db.open(dbPath, &error)) {
        QMessageBox::critical(nullptr, QStringLiteral("zeitbenutzer"),
                              QStringLiteral("Could not open database:\n%1\n\n%2")
                                  .arg(dbPath, error));
        return 1;
    }

    zb::Store store(db);
    zb::MainWindow window(store);
    window.resize(1100, 700);
    window.show();
    return app.exec();
}
