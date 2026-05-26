#pragma once

// App settings, persisted as an INI in the app-data dir (NOT via QSettings'
// org/app default — that would change AppDataLocation and orphan the DB).

#include <QDate>
#include <QSettings>
#include <QString>

namespace zb {

class Settings {
public:
    static Settings& instance();

    int  weekStartDay() const;          // Qt weekday: 1=Mon .. 7=Sun
    void setWeekStartDay(int day);

    int  snapMinutes() const;           // calendar drag snap (15 or 30)
    void setSnapMinutes(int minutes);

    QString dataLocation() const;       // dir holding the DB + this ini

private:
    Settings();
    QSettings m_s;
};

// First day of the week containing `d`, per the configured week-start.
QDate weekStartFor(const QDate& d);

} // namespace zb
