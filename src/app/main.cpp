#include "app/main_window.h"
#include "app/theme.h"
#include "app/typography.h"
#include "storage/database.h"
#include "storage/store.h"

#include <QApplication>
#include <QDir>
#include <QMessageBox>
#include <QStandardPaths>

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("zeitbenutzer"));

    zb::registerBundledFonts();
    QApplication::setFont(zb::uiFont()); // chrome + preview; editor overrides to mono
    zb::applyTheme(app, zb::lightTheme());

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
