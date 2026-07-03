#pragma once

// A pragmatic, dependency-free iCalendar (RFC 5545) reader — enough to overlay
// read-only external calendars (a local .ics file or a fetched ICS URL) on the
// week grid. Pure QtCore, so it's unit-testable headless.
//
// Supported: VEVENT with UID / SUMMARY / LOCATION / DTSTART / DTEND / DURATION,
// all-day (VALUE=DATE) events, UTC (...Z), TZID=<IANA zone> and floating-local
// times, RRULE (via the project's RRule subset) and EXDATE. Line unfolding and
// TEXT unescaping are handled.
//
// Best-effort by design — these are silently skipped rather than failing the
// parse: custom VTIMEZONE definitions (we trust the system zone DB via
// QTimeZone), RDATE, RECURRENCE-ID overrides, and RRULE features outside the
// RRule subset (see recurrence.h).

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QTimeZone>

namespace zb {

struct ICalEvent {
    QString   uid;
    QString   summary;
    QString   location;
    QDateTime start;          // UTC
    QDateTime end;            // UTC
    bool      allDay = false;

    // Recurrence (retained on parse, cleared on expansion). `zone` is the
    // event's source zone, used so recurrence keeps a stable wall-clock time
    // across DST rather than drifting in UTC.
    QString        rrule;     // empty when non-recurring
    QList<QDateTime> exDates; // EXDATE instants (UTC)
    QTimeZone      zone = QTimeZone::utc();
};

// Parse an ICS payload into raw events (recurring ones not yet expanded).
// Returns the events parsed; sets *error to the first hard problem (e.g. not an
// iCalendar stream) but still returns whatever parsed cleanly.
QList<ICalEvent> parseICalendar(const QByteArray& data, QString* error = nullptr);

// Expand recurring events into concrete instances overlapping [winStart,winEnd]
// and pass non-recurring events through (those that overlap). Instance UIDs are
// suffixed with the occurrence date so each is unique. Recurrence fields are
// cleared on the returned events.
QList<ICalEvent> expandICalEvents(const QList<ICalEvent>& events,
                                  const QDateTime& winStart,
                                  const QDateTime& winEnd);

// The writer counterpart: serialize concrete (already-expanded) events as an
// RFC 5545 VCALENDAR. Timed events emit UTC DTSTART/DTEND; all-day events emit
// VALUE=DATE (DTEND exclusive). TEXT fields are escaped. Round-trips through
// parseICalendar. Used by the calendar's week export.
QByteArray writeICalendar(const QList<ICalEvent>& events);

} // namespace zb
