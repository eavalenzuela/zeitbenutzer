#pragma once

// A pragmatic subset of RFC 5545 RRULE — enough for routines and repeating
// blocks/tasks without dragging in a full iCalendar implementation.
//
// Supported keys: FREQ (DAILY|WEEKLY|MONTHLY), INTERVAL, BYDAY (MO..SU),
// BYMONTHDAY, COUNT. The recurrence end date (UNTIL) is carried on the series
// row, not here, so it can be edited independently.
//
// Example: "FREQ=WEEKLY;INTERVAL=1;BYDAY=MO,WE,FR;COUNT=12"

#include <QDateTime>
#include <QList>
#include <QString>
#include <optional>

namespace zb {

struct RRule {
    enum class Freq { Daily, Weekly, Monthly };

    Freq       freq = Freq::Daily;
    int        interval = 1;
    QList<int> byDay;        // Qt weekday numbers 1=Mon .. 7=Sun (FREQ=WEEKLY)
    int        byMonthDay = 0; // 1..31, 0 = use the anchor's day (FREQ=MONTHLY)
    int        count = 0;    // 0 = unbounded

    // Parse a rule string. Returns nullopt if FREQ is missing/invalid.
    static std::optional<RRule> parse(const QString& s);

    // Canonical serialization (round-trips with parse()).
    QString toString() const;

    // Expand occurrence start datetimes within [winStart, winEnd] (inclusive).
    // `anchorStart` is the first occurrence (DTSTART): every occurrence keeps
    // its time-of-day. `until` (if valid) caps the series; COUNT is honoured
    // from the anchor regardless of the window.
    QList<QDateTime> expand(const QDateTime& anchorStart,
                            const QDateTime& until,
                            const QDateTime& winStart,
                            const QDateTime& winEnd) const;
};

} // namespace zb
