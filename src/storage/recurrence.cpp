#include "storage/recurrence.h"

namespace zb {

namespace {

// Monday of the ISO week containing `d`.
QDate weekStart(const QDate& d)
{
    return d.addDays(-(d.dayOfWeek() - 1)); // dayOfWeek: Mon=1..Sun=7
}

int parseWeekday(const QString& tok)
{
    static const QString days[] = {QStringLiteral("MO"), QStringLiteral("TU"),
                                   QStringLiteral("WE"), QStringLiteral("TH"),
                                   QStringLiteral("FR"), QStringLiteral("SA"),
                                   QStringLiteral("SU")};
    for (int i = 0; i < 7; ++i)
        if (tok == days[i])
            return i + 1; // 1=Mon..7=Sun
    return 0;
}

const char* weekdayToken(int wd)
{
    static const char* days[] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
    return (wd >= 1 && wd <= 7) ? days[wd - 1] : "MO";
}

} // namespace

std::optional<RRule> RRule::parse(const QString& s)
{
    RRule r;
    bool haveFreq = false;

    const QStringList parts = s.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = part.left(eq).trimmed().toUpper();
        const QString val = part.mid(eq + 1).trimmed();

        if (key == QLatin1String("FREQ")) {
            const QString v = val.toUpper();
            if (v == QLatin1String("DAILY")) r.freq = Freq::Daily;
            else if (v == QLatin1String("WEEKLY")) r.freq = Freq::Weekly;
            else if (v == QLatin1String("MONTHLY")) r.freq = Freq::Monthly;
            else return std::nullopt;
            haveFreq = true;
        } else if (key == QLatin1String("INTERVAL")) {
            r.interval = qMax(1, val.toInt());
        } else if (key == QLatin1String("COUNT")) {
            r.count = qMax(0, val.toInt());
        } else if (key == QLatin1String("BYMONTHDAY")) {
            r.byMonthDay = val.toInt();
        } else if (key == QLatin1String("BYDAY")) {
            for (const QString& tok : val.toUpper().split(QLatin1Char(','),
                                                          Qt::SkipEmptyParts)) {
                const int wd = parseWeekday(tok.trimmed());
                if (wd && !r.byDay.contains(wd))
                    r.byDay.append(wd);
            }
            std::sort(r.byDay.begin(), r.byDay.end());
        }
    }

    if (!haveFreq)
        return std::nullopt;
    return r;
}

QString RRule::toString() const
{
    QStringList out;
    switch (freq) {
    case Freq::Daily:   out << QStringLiteral("FREQ=DAILY"); break;
    case Freq::Weekly:  out << QStringLiteral("FREQ=WEEKLY"); break;
    case Freq::Monthly: out << QStringLiteral("FREQ=MONTHLY"); break;
    }
    if (interval != 1)
        out << QStringLiteral("INTERVAL=%1").arg(interval);
    if (!byDay.isEmpty()) {
        QStringList toks;
        for (int wd : byDay)
            toks << QLatin1String(weekdayToken(wd));
        out << QStringLiteral("BYDAY=%1").arg(toks.join(QLatin1Char(',')));
    }
    if (byMonthDay > 0)
        out << QStringLiteral("BYMONTHDAY=%1").arg(byMonthDay);
    if (count > 0)
        out << QStringLiteral("COUNT=%1").arg(count);
    return out.join(QLatin1Char(';'));
}

QList<QDateTime> RRule::expand(const QDateTime& anchorStart,
                               const QDateTime& until,
                               const QDateTime& winStart,
                               const QDateTime& winEnd) const
{
    QList<QDateTime> result;
    if (!anchorStart.isValid())
        return result;

    const QDate aDate = anchorStart.date();
    const QTime tod   = anchorStart.time();
    const QDate aWeek = weekStart(aDate);
    const int   ivl   = qMax(1, interval);

    int produced = 0;
    QDate d = aDate;

    // Fast-forward the walk to just before the window. Only when there is no
    // COUNT cap: with one, every occurrence from the anchor must be produced
    // (and counted) to know where the cap lands. The jump is always a whole
    // number of pattern periods, so the modulo alignment below is preserved.
    if (count <= 0 && winStart.isValid() && winStart.date() > d) {
        const qint64 daysAhead = d.daysTo(winStart.date());
        switch (freq) {
        case Freq::Daily:
            d = d.addDays((daysAhead / ivl) * ivl);
            break;
        case Freq::Weekly:
            d = d.addDays((daysAhead / (7ll * ivl)) * (7ll * ivl));
            break;
        case Freq::Monthly: {
            const QDate w = winStart.date();
            const int months =
                (w.year() - aDate.year()) * 12 + (w.month() - aDate.month());
            const int jump = (qMax(0, months) / ivl) * ivl;
            // Land on the 1st of an aligned month; the day-of-month match
            // below finds the occurrence. Never move backwards past anchor.
            const QDate cand =
                QDate(aDate.year(), aDate.month(), 1).addMonths(jump);
            if (cand > d)
                d = cand;
            break;
        }
        }
    }

    // Safety bound on the day-by-day walk (~500 years) so an open-ended rule
    // with a far-future window can never loop forever.
    const int kMaxDays = 366 * 500;

    for (int i = 0; i < kMaxDays; ++i, d = d.addDays(1)) {
        const QDateTime dt(d, tod);
        if (winEnd.isValid() && dt > winEnd)
            break;

        bool match = false;
        switch (freq) {
        case Freq::Daily: {
            const qint64 days = aDate.daysTo(d);
            match = days >= 0 && (days % ivl) == 0;
            break;
        }
        case Freq::Weekly: {
            const qint64 weeks = aWeek.daysTo(weekStart(d)) / 7;
            if (weeks < 0 || (weeks % ivl) != 0)
                break;
            match = byDay.isEmpty() ? (d.dayOfWeek() == aDate.dayOfWeek())
                                    : byDay.contains(d.dayOfWeek());
            break;
        }
        case Freq::Monthly: {
            const int target = byMonthDay > 0 ? byMonthDay : aDate.day();
            const int months =
                (d.year() - aDate.year()) * 12 + (d.month() - aDate.month());
            match = months >= 0 && (months % ivl) == 0 && d.day() == target;
            break;
        }
        }
        if (!match)
            continue;

        if (until.isValid() && dt > until)
            break;
        ++produced;
        if (count > 0 && produced > count)
            break;

        if (!winStart.isValid() || dt >= winStart)
            result.append(dt);
    }
    return result;
}

} // namespace zb
