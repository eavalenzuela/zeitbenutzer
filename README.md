# zeitbenutzer

A self-contained desktop app where **time and projects are co-equal anchors**.
You plan your time in blocks, then **reconcile** what actually happened — and
every project shows the time spent on it as the inverse of that schedule.

Successor in spirit to `skein`, which organized notes spatially; zeitbenutzer
organizes them temporally. See [`DESIGN.md`](./DESIGN.md) for the full design.

## Status

- **Module 0 — storage** (headless). SQLite is the source of truth for structure
  (projects, notes, tasks, blocks, recurring series, plan/actual times, links);
  markdown only for note bodies.
- **Module 1 — projects + notes** (GUI). Project tree (nestable,
  create/rename/delete) | markdown editor with live preview and debounced
  autosave. Bundled fonts + a centralized light theme (dark theme later via
  `theme.h`).
- **Module 2 — tasks** (GUI). First-class tasks per project in a Notes/Tasks
  tabbed pane: create/rename/delete, status (todo/doing/done/cancelled), and a
  per-task time estimate.
- **Module 3 — calendar** (GUI). Week time-grid (7 days × 24h): drag-to-create
  (via a title · project · repeat dialog), drag-to-move, edge-resize, 15-min
  snap, week navigation. Recurring series render as dashed ↻ phantom
  occurrences; editing one materializes it. Double-click to edit/delete (single
  occurrence vs whole series). Center toggles Calendar ↔ Workspace.
- **Module 4 — reconcile** (GUI). Each day splits into `plan | actual` lanes.
  Double-click a block to set status (Planned/Done/Skipped/Carried) and actual
  times; the actual span renders green in the right lane, so plan vs. outcome
  sit side by side. **Carry over** clones an unfinished block onto a chosen day
  (default tomorrow), leaving the original marked `carried`. The calendar's
  **Review today** button runs a guided evening walk through the day's
  still-unreconciled blocks, one at a time.
- **Module 5 — rollups** (GUI). Workspace **Time** tab: per-project planned vs.
  actual, this-week vs. all-time, optionally recursing sub-projects — the
  inverse of the calendar (where time goes, per project).
- **Module 6 — external calendars** (GUI). Read-only overlay of outside
  calendars on the week grid. Add sources via **App → Calendars…**: a local
  `.ics` file or an **ICS URL** — which covers Google / iCloud / Outlook via
  their "secret iCal address", so no OAuth is needed. All-day events stack in a
  band above the grid; timed events render faint behind your blocks. They never
  count toward reconcile or rollups. **Right-click → Adopt into block…** pulls
  one into a real, trackable block. Sources refresh on launch and via **Refresh
  now**; failures surface (status bar / dialog) rather than failing silently.

The core build plan (modules 0–5) is complete, plus module 6 (external
calendars).

**Settings** (App → Settings…): light/dark **theme**, week-start day (Mon/Sun),
and calendar snap interval (15/30 min), persisted to `settings.ini` in the
app-data dir. Theming uses Qt's Fusion style + a palette for consistent
rendering across every widget and platform.

**Project colors:** each project has a color (right-click → Set colour…, or a
stable auto-color derived from its id). Shown as a swatch in the tree and used
to tint calendar blocks by project. Blocks with **no project** render hatched
grey with a ⚠ marker so they stand out (they don't appear in rollups).

Calendar niceties: a red **current-time line** on today's column (kept live by
a once-a-minute repaint), hover **tooltips** on blocks and external events
(title · project · plan/actual span · status), a **"Repeat for N times"**
option (RRULE `COUNT`) in the create dialog, and keyboard navigation —
**←/→** previous/next week, **T**/Home today (when the calendar has focus),
**Esc** cancels an in-progress drag. The toolbar label shows the visible
week's **plan vs done totals** at a glance. Reconciling a block whose actual
end is before its start (worked past midnight) rolls the end into the next
day rather than recording a negative span.

**Tasks → calendar:** the task list's **Schedule…** action turns the selected
task into a real block — pick a date, start time and duration (prefilled from
the task's estimate); the block links back to the task via `block_task` and
carries the task's project for time attribution.

**Archiving:** right-click a project → **Archive** parks it (and its subtree)
out of the tree without touching its history — rollups still count it. The
tree's **Show archived** toggle lists archived projects greyed/italic;
right-click → **Unarchive** restores them.

**Find in notes** (App → Find in notes…, `Ctrl+Shift+F`): live search over
note titles and bodies; activating a match jumps straight to the note, the
same way clicking a `[[wikilink]]` does.

**ICS export:** the calendar's **Export…** button saves the visible week's
planned blocks (materialized and phantom alike) as a standard `.ics` file —
the write counterpart of the module-6 reader, so your plan isn't locked in.

## Stack

- **Qt6 / C++20** (Core, Sql, Widgets, Network), CMake. Target: **macOS +
  Linux**.
- **SQLite** via Qt's bundled `QSQLITE` driver (`Qt6::Sql`) — no extra deps.
- **iCalendar** reading is a small dependency-free parser (`storage/ical`); ICS
  feeds are fetched with `Qt6::Network`. No third-party libraries.
- **Bundled fonts** (OFL 1.1, embedded as Qt resources): Source Code Pro
  (editor) + Source Sans 3 (UI/preview), so rendering is identical everywhere.

## Build & test

Prerequisites: a C++20 compiler, CMake ≥ 3.21, and Qt6 (`Core`, `Sql`,
`Widgets`, `Network`).

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure   # runs storage_smoketest
```

## Layout

```
.
├── DESIGN.md              # canonical design + phased build plan
├── src/storage/           # module 0: schema, stores, recurrence
│   ├── types.h            # entity structs mirroring the schema
│   ├── database.{h,cpp}   # connection + migrations
│   ├── recurrence.{h,cpp} # RRULE subset: parse / serialize / expand
│   ├── ical.{h,cpp}       # module 6: iCalendar (RFC 5545) reader/writer + expander
│   └── store.{h,cpp}      # CRUD, materialization, rollups, search, external events
├── src/app/               # module 1: GUI shell + panels
│   ├── main.cpp           # opens DB at app-data dir, shows MainWindow
│   ├── main_window.{h,cpp}        # three-pane shell, wiring
│   ├── project_tree_panel.{h,cpp} # nestable tree, create/rename/delete
│   ├── note_list_panel.{h,cpp}    # notes for selected project
│   └── note_editor.{h,cpp}        # markdown editor + live preview + autosave
└── tests/                 # per-module smoke tests (storage, app/offscreen)
```

## Build plan

0. **storage** (this) → 1. projects+notes → 2. tasks → 3. calendar+recurrence
   (GUI) → 4. reconcile → 5. rollups view → 6. external calendars. See
   `DESIGN.md`.
