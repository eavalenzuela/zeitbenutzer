#include "storage/ical.h"

#include "storage/recurrence.h"

#include <QMap>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace zb {

namespace {

// --- line unfolding ----------------------------------------------------------
// RFC 5545 §3.1: a long content line is folded by inserting CRLF followed by a
// single space or tab. Undo that, normalising CRLF/LF/CR.
QStringList unfold(const QByteArray& data)
{
    const QString text = QString::fromUtf8(data);
    QStringList raw = text.split(QRegularExpression(QStringLiteral("\r\n|\r|\n")));
    QStringList out;
    for (const QString& line : raw) {
        if (!line.isEmpty() && (line.at(0) == QLatin1Char(' ')
                                || line.at(0) == QLatin1Char('\t'))
            && !out.isEmpty()) {
            out.last() += line.mid(1);
        } else {
            out.append(line);
        }
    }
    return out;
}

// TEXT value unescaping (RFC 5545 §3.3.11): \n \, \; \\.
QString unescapeText(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\') && i + 1 < s.size()) {
            const QChar n = s.at(++i);
            if (n == QLatin1Char('n') || n == QLatin1Char('N'))
                out += QLatin1Char('\n');
            else
                out += n; // \, \; \\ -> literal
        } else {
            out += c;
        }
    }
    return out;
}

// One content line split into NAME, its parameters, and the raw value.
struct Line {
    QString name;
    QMap<QString, QString> params; // upper-cased keys
    QString value;
};

Line splitLine(const QString& line)
{
    Line l;
    const int colon = line.indexOf(QLatin1Char(':'));
    if (colon < 0) {
        l.name = line.toUpper();
        return l;
    }
    l.value = line.mid(colon + 1);
    const QStringList head =
        line.left(colon).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    if (!head.isEmpty())
        l.name = head.first().toUpper();
    for (int i = 1; i < head.size(); ++i) {
        const int eq = head.at(i).indexOf(QLatin1Char('='));
        if (eq > 0)
            l.params.insert(head.at(i).left(eq).toUpper(),
                            head.at(i).mid(eq + 1));
    }
    return l;
}

// Resolve a TZID parameter to a zone; fall back to local time when unknown.
QTimeZone zoneFor(const QString& tzid)
{
    if (tzid.isEmpty())
        return QTimeZone::systemTimeZone();
    const QByteArray id = tzid.toUtf8();
    QTimeZone z(id);
    return z.isValid() ? z : QTimeZone::systemTimeZone();
}

// Parse a DATE or DATE-TIME value. Sets *allDay for a VALUE=DATE form. Returns
// a UTC QDateTime; *zone receives the source zone (for recurrence wall-time).
QDateTime parseDateTime(const Line& l, bool* allDay, QTimeZone* zone)
{
    const QString v = l.value.trimmed();
    const bool isDate = (l.params.value(QStringLiteral("VALUE")).toUpper()
                             == QStringLiteral("DATE"))
                        || (v.size() == 8 && !v.contains(QLatin1Char('T')));
    if (allDay)
        *allDay = isDate;

    if (isDate) {
        const QDate d = QDate::fromString(v.left(8), QStringLiteral("yyyyMMdd"));
        // All-day events are floating (date-only). Anchor at local midnight so
        // the calendar date survives the UTC-epoch round-trip in storage and
        // renders on the right day regardless of viewer offset.
        const QTimeZone local = QTimeZone::systemTimeZone();
        if (zone)
            *zone = local;
        return QDateTime(d, QTime(0, 0), local);
    }

    const bool utc = v.endsWith(QLatin1Char('Z'));
    const QString core = utc ? v.left(v.size() - 1) : v;
    const QDateTime naive =
        QDateTime::fromString(core, QStringLiteral("yyyyMMddTHHmmss"));
    if (utc) {
        if (zone)
            *zone = QTimeZone::utc();
        return QDateTime(naive.date(), naive.time(), QTimeZone::utc());
    }
    const QTimeZone z = zoneFor(l.params.value(QStringLiteral("TZID")));
    if (zone)
        *zone = z;
    return QDateTime(naive.date(), naive.time(), z).toUTC();
}

// ISO 8601 duration (subset): PnW, PnDTnHnMnS. Returns seconds.
qint64 parseDuration(const QString& s)
{
    qint64 secs = 0;
    bool inTime = false;
    QString num;
    for (const QChar c : s) {
        if (c == QLatin1Char('P')) {
            continue;
        } else if (c == QLatin1Char('T')) {
            inTime = true;
        } else if (c.isDigit()) {
            num += c;
        } else {
            const qint64 n = num.toLongLong();
            num.clear();
            if (c == QLatin1Char('W')) secs += n * 7 * 86400;
            else if (c == QLatin1Char('D')) secs += n * 86400;
            else if (c == QLatin1Char('H')) secs += n * 3600;
            else if (c == QLatin1Char('M')) secs += inTime ? n * 60 : n * 2592000;
            else if (c == QLatin1Char('S')) secs += n;
        }
    }
    return secs;
}

} // namespace

QList<ICalEvent> parseICalendar(const QByteArray& data, QString* error)
{
    QList<ICalEvent> events;
    const QStringList lines = unfold(data);
    if (lines.isEmpty()
        || std::none_of(lines.begin(), lines.end(), [](const QString& l) {
               return l.trimmed().toUpper() == QStringLiteral("BEGIN:VCALENDAR");
           })) {
        if (error)
            *error = QStringLiteral("not an iCalendar stream");
        return events;
    }

    bool inEvent = false;
    ICalEvent cur;
    QDateTime dtEnd;       // resolved separately so DURATION can fill a gap
    qint64 duration = 0;   // seconds, when DURATION given instead of DTEND
    bool haveEnd = false;

    for (const QString& rawLine : lines) {
        const Line l = splitLine(rawLine);
        if (l.name == QStringLiteral("BEGIN")
            && l.value.trimmed().toUpper() == QStringLiteral("VEVENT")) {
            inEvent = true;
            cur = ICalEvent{};
            dtEnd = QDateTime();
            duration = 0;
            haveEnd = false;
            continue;
        }
        if (l.name == QStringLiteral("END")
            && l.value.trimmed().toUpper() == QStringLiteral("VEVENT")) {
            inEvent = false;
            if (cur.start.isValid()) {
                if (haveEnd)
                    cur.end = dtEnd;
                else if (duration > 0)
                    cur.end = cur.start.addSecs(duration);
                else if (cur.allDay)
                    cur.end = cur.start.addDays(1);
                else
                    cur.end = cur.start; // zero-length fallback
                events.append(cur);
            }
            continue;
        }
        if (!inEvent)
            continue;

        if (l.name == QStringLiteral("UID")) {
            cur.uid = l.value.trimmed();
        } else if (l.name == QStringLiteral("SUMMARY")) {
            cur.summary = unescapeText(l.value);
        } else if (l.name == QStringLiteral("LOCATION")) {
            cur.location = unescapeText(l.value);
        } else if (l.name == QStringLiteral("DTSTART")) {
            cur.start = parseDateTime(l, &cur.allDay, &cur.zone);
        } else if (l.name == QStringLiteral("DTEND")) {
            bool ad = false;
            dtEnd = parseDateTime(l, &ad, nullptr);
            haveEnd = true;
        } else if (l.name == QStringLiteral("DURATION")) {
            duration = parseDuration(l.value.trimmed());
        } else if (l.name == QStringLiteral("RRULE")) {
            cur.rrule = l.value.trimmed();
        } else if (l.name == QStringLiteral("EXDATE")) {
            for (const QString& tok :
                 l.value.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                Line el = l;
                el.value = tok.trimmed();
                bool ad = false;
                cur.exDates.append(parseDateTime(el, &ad, nullptr));
            }
        }
    }
    return events;
}

QList<ICalEvent> expandICalEvents(const QList<ICalEvent>& events,
                                  const QDateTime& winStart,
                                  const QDateTime& winEnd)
{
    QList<ICalEvent> out;
    for (const ICalEvent& e : events) {
        if (!e.start.isValid())
            continue;

        if (e.rrule.isEmpty()) {
            // Non-recurring: keep it if it overlaps the window.
            if (e.end >= winStart && e.start <= winEnd)
                out.append(e);
            continue;
        }

        const auto rule = RRule::parse(e.rrule);
        if (!rule) {
            // Unparseable rule: surface at least the anchor instance.
            if (e.end >= winStart && e.start <= winEnd) {
                ICalEvent first = e;
                first.rrule.clear();
                out.append(first);
            }
            continue;
        }

        // Expand on the source zone's wall clock so a 09:00 event stays 09:00
        // across DST, then convert each instance back to UTC.
        const QTimeZone z = e.zone.isValid() ? e.zone : QTimeZone::utc();
        const qint64 dur = e.start.secsTo(e.end);
        const QDateTime anchorWall = e.start.toTimeZone(z);
        const QDate anchorDate = anchorWall.date();
        const QTime tod = anchorWall.time();

        // Window expressed in the same wall clock, widened a day each side so a
        // boundary-straddling instance isn't dropped.
        const QDateTime wsWall = winStart.toTimeZone(z).addDays(-1);
        const QDateTime weWall = winEnd.toTimeZone(z).addDays(1);
        const QDateTime anchorLocal(anchorDate, tod); // local-spec for RRule

        const QList<QDateTime> wallHits =
            rule->expand(anchorLocal, QDateTime(),
                         QDateTime(wsWall.date(), wsWall.time()),
                         QDateTime(weWall.date(), weWall.time()));

        for (const QDateTime& wall : wallHits) {
            const QDateTime instUtc =
                QDateTime(wall.date(), wall.time(), z).toUTC();
            const QDateTime instEnd = instUtc.addSecs(dur);
            if (instEnd < winStart || instUtc > winEnd)
                continue;
            const bool excluded =
                std::any_of(e.exDates.begin(), e.exDates.end(),
                            [&](const QDateTime& ex) {
                                return qAbs(ex.secsTo(instUtc)) < 60;
                            });
            if (excluded)
                continue;

            ICalEvent inst = e;
            inst.rrule.clear();
            inst.exDates.clear();
            inst.start = instUtc;
            inst.end = instEnd;
            inst.uid = e.uid + QStringLiteral("#")
                       + wall.date().toString(QStringLiteral("yyyyMMdd"));
            out.append(inst);
        }
    }
    return out;
}

namespace {

// TEXT value escaping (RFC 5545 §3.3.11): the inverse of unescapeText.
QString escapeText(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (c == QLatin1Char('\\'))
            out += QLatin1String("\\\\");
        else if (c == QLatin1Char(';'))
            out += QLatin1String("\\;");
        else if (c == QLatin1Char(','))
            out += QLatin1String("\\,");
        else if (c == QLatin1Char('\n'))
            out += QLatin1String("\\n");
        else if (c != QLatin1Char('\r'))
            out += c;
    }
    return out;
}

QString utcStamp(const QDateTime& dt)
{
    return dt.toUTC().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
}

} // namespace

QByteArray writeICalendar(const QList<ICalEvent>& events)
{
    // Concrete instances only — recurrence is expanded before export, so no
    // RRULE/EXDATE lines are emitted. Lines end CRLF per the RFC.
    const QString crlf = QStringLiteral("\r\n");
    QString out;
    out += QStringLiteral("BEGIN:VCALENDAR") + crlf;
    out += QStringLiteral("VERSION:2.0") + crlf;
    out += QStringLiteral("PRODID:-//zeitbenutzer//EN") + crlf;
    out += QStringLiteral("CALSCALE:GREGORIAN") + crlf;

    const QString dtstamp = utcStamp(QDateTime::currentDateTimeUtc());
    for (const ICalEvent& e : events) {
        if (!e.start.isValid() || !e.end.isValid())
            continue;
        out += QStringLiteral("BEGIN:VEVENT") + crlf;
        out += QStringLiteral("UID:") + escapeText(e.uid) + crlf;
        out += QStringLiteral("DTSTAMP:") + dtstamp + crlf;
        if (e.allDay) {
            out += QStringLiteral("DTSTART;VALUE=DATE:")
                   + e.start.date().toString(QStringLiteral("yyyyMMdd")) + crlf;
            out += QStringLiteral("DTEND;VALUE=DATE:")
                   + e.end.date().toString(QStringLiteral("yyyyMMdd")) + crlf;
        } else {
            out += QStringLiteral("DTSTART:") + utcStamp(e.start) + crlf;
            out += QStringLiteral("DTEND:") + utcStamp(e.end) + crlf;
        }
        out += QStringLiteral("SUMMARY:") + escapeText(e.summary) + crlf;
        if (!e.location.isEmpty())
            out += QStringLiteral("LOCATION:") + escapeText(e.location) + crlf;
        out += QStringLiteral("END:VEVENT") + crlf;
    }

    out += QStringLiteral("END:VCALENDAR") + crlf;
    return out.toUtf8();
}

} // namespace zb
