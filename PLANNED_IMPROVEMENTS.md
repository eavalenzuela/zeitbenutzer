# Planned improvements & features

Plan for this round of work. Improvements sharpen what exists; features close
gaps the design doc already points at (task pool → calendar, archived projects,
data portability).

## Improvements (10)

1. **Fast-forward recurrence expansion** — `RRule::expand` walks day-by-day
   from the series anchor; a routine anchored a year ago costs ~365 wasted
   iterations per series on every calendar reload. Skip ahead to the query
   window when no `COUNT` cap forces counting from the anchor.
2. **Past-midnight actual spans** — reconciling a block whose actual end
   time-of-day is before its start (worked past midnight) currently produces a
   negative span that corrupts rollups; roll the end into the next day.
3. **Rollup guards against negative spans** — `plannedMinutes`/`actualMinutes`
   sum raw `end − start`; wrap in `MAX(…, 0)` so a bad legacy row can never
   subtract time from a project's total.
4. **Live current-time line** — the calendar's red now-line only moves when
   something else forces a repaint; a once-a-minute timer keeps it (and the
   today-column highlight) current.
5. **Confirm note deletion** — "Delete note" is a silent, irreversible
   one-click destroy (projects already confirm); ask first, naming the note.
6. **Calendar keyboard navigation** — Left/Right for prev/next week, T/Home
   for today (scoped to the calendar), and Escape cancels an in-progress
   block drag instead of committing it.
7. **Hover tooltips on the grid** — blocks and external events show
   title · project · span · status · recurrence on hover; today the grid says
   nothing beyond what fits inside the rectangle.
8. **Transactional multi-statement store ops** — `deleteBlockSeries` and
   `carryOverBlock` run several statements without a transaction and can
   half-apply on failure; wrap both.
9. **Extend the smoke tests** — cover fast-forward expansion parity, archive
   filtering, note search, the negative-span guard, ICS writer round-trip, and
   past-midnight reconcile.
10. **Document the new behavior** — README gains the new features, shortcuts,
    and reconcile semantics so the docs keep matching the app.

## New features (5)

11. **Schedule a task onto the calendar** — a "Schedule…" action on the task
    list opens a date/time/duration dialog (duration prefilled from the
    estimate) and creates a linked block via `block_task`; closes the design's
    task-pool → calendar loop with the so-far-unused `linkBlockTask`.
12. **Project archiving** — the schema has had `archived` since v1 with no UI;
    add Archive/Unarchive to the tree's context menu (cascading to
    descendants), a "Show archived" toggle, and grey/italic rendering.
13. **Global note search** — `Store::searchNotes` (title + body, escaped LIKE)
    behind a Find-in-notes dialog (Ctrl+Shift+F); activating a result jumps to
    the note through the same path wikilinks use.
14. **ICS export** — a `writeICalendar` counterpart to the existing reader,
    plus an "Export…" button that saves the visible week's planned blocks as a
    standard `.ics`; your plan stops being locked in.
15. **Week totals at a glance** — the calendar toolbar label appends planned
    vs done hours for the visible week, surfacing the plan/reconcile payoff
    without opening the Time tab.
