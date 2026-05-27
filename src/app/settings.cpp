#include "app/settings.h"

#include <QStandardPaths>

namespace zb {

namespace {
QString iniPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
           + QStringLiteral("/settings.ini");
}
} // namespace

Settings::Settings() : m_s(iniPath(), QSettings::IniFormat) {}

Settings& Settings::instance()
{
    static Settings s;
    return s;
}

int Settings::weekStartDay() const
{
    const int d = m_s.value(QStringLiteral("calendar/weekStartDay"), 1).toInt();
    return (d >= 1 && d <= 7) ? d : 1;
}
void Settings::setWeekStartDay(int day)
{
    m_s.setValue(QStringLiteral("calendar/weekStartDay"), qBound(1, day, 7));
}

int Settings::snapMinutes() const
{
    const int m = m_s.value(QStringLiteral("calendar/snapMinutes"), 15).toInt();
    return m > 0 ? m : 15;
}
void Settings::setSnapMinutes(int minutes)
{
    m_s.setValue(QStringLiteral("calendar/snapMinutes"), minutes > 0 ? minutes : 15);
}

QString Settings::themeName() const
{
    return m_s.value(QStringLiteral("ui/theme"), QStringLiteral("light")).toString();
}
void Settings::setThemeName(const QString& name)
{
    m_s.setValue(QStringLiteral("ui/theme"), name);
}

QString Settings::imageStorageBackend() const
{
    const QString b =
        m_s.value(QStringLiteral("images/backend"), QStringLiteral("blob")).toString();
    return b == QStringLiteral("disk") ? b : QStringLiteral("blob");
}
void Settings::setImageStorageBackend(const QString& backend)
{
    m_s.setValue(QStringLiteral("images/backend"),
                 backend == QStringLiteral("disk") ? QStringLiteral("disk")
                                                   : QStringLiteral("blob"));
}

QString Settings::dataLocation() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QDate weekStartFor(const QDate& d)
{
    const int first = Settings::instance().weekStartDay();
    const int off = (d.dayOfWeek() - first + 7) % 7;
    return d.addDays(-off);
}

} // namespace zb
