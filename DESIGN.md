# zeitbenutzer — Design Document

A self-contained desktop app where **time and projects are co-equal anchors**.
You plan your time in blocks, then **reconcile** what actually happened — and
every project shows the time spent on it as the inverse of that schedule.

Working name: *zeitbenutzer* ("time-user"). Successor in spirit to `skein`,
which organized notes spatially (bookshelf/desk). zeitbenutzer organizes them
**temporally**: a calendar is the front door, projects and notes are what the
time points into.

## Philosophy

- **Time and structure are peers, not master/servant.** A *block* answers
  "what am I doing now"; a *project* answers "where has my time gone." Each is
  the other's inverse, joined by a link, not nested inside it.
- **Plan, then reconcile.** The product is the daily loop: plan intended work
  in the morning, confirm what actually happened in the evening. Planned and
  actual time are two distinct faces of the same block. The reconcile step is
  what makes this more than a calendar.
- **The schedule is structured data, not text.** A bidirectional plan/actual
  link graph does not belong in YAML frontmatter. SQLite owns the structure;
  markdown owns only prose.
- **Local and self-contained.** No accounts and no write-back sync; your data
  is a single SQLite file you can back up. External calendars are read-only —
  pulled from an `.ics` file or ICS URL and overlaid, never edited or uploaded
  (module 6).

## Scope

**In scope (v1)**
- Nestable **projects** (folders within folders).
- **Notes** (markdown prose), every note lives inside a project.
- **Tasks** as a first-class type, distinct from notes — small schedulable atoms.
- **Blocks**: scheduled spans of time with a *planned* face and an *actual* face.
- **Recurrence**: repeating blocks *and* tasks (RRULE-based), with per-occurrence
  edits and skips. Present in v1, not deferred.
- Day and week **calendar views** with drag-to-create / drag-to-resize blocks.
- The **reconcile** flow: confirm/adjust/skip/carry-over planned blocks.
- **Rollups**: per-project planned-vs-actual time, by week and all-time,
  recursing subprojects.

**Out of scope (v1)**
- External calendar sync (Google/CalDAV) — parked; possible later as read-only.
- Embeddings / semantic search / Claude chat (skein had these; revisit later).
- Mobile / sync across machines.

---

## Stack

- **Qt6 / C++20** (Core, Sql, Widgets), CMake; per-module static libraries, an
  `app` module that links them, per-module smoke tests.
- **SQLite** as the source of truth for structure, via Qt's bundled `QSQLITE`
  driver (`Qt6::Sql`) — no extra dependency. (Decided in module 0.)
- **Markdown** note bodies via Qt6 `QTextEdit::setMarkdown()` / `QTextBrowser`
  for edit+preview without a JS stack.
- **`QGraphicsView` / `QGraphicsScene`** for the calendar time grid — blocks,
  resize handles, snap-to-15min, and the plan-vs-actual overlay are all scene
  items. This is the reason Qt was chosen over a webview shell.

## Typography

Fonts are **bundled** (embedded as Qt resources, `assets/fonts.qrc`, registered
at startup via `QFontDatabase::addApplicationFont`) so the app renders
identically on every platform regardless of installed system fonts. Both are
**SIL Open Font License 1.1** — explicitly redistributable inside an app:

- **Source Code Pro** — editor body / code. Chosen for glyph disambiguation
  (serifed `I`, distinct `l`/`1`/`|`).
- **Source Sans 3** — UI chrome + rendered markdown preview. Pairs with Source
  Code Pro (same Adobe superfamily). Regular/Bold/Italic/BoldItalic bundled so
  preview emphasis resolves to real cuts, not faux styles.

License texts ship in `assets/fonts/OFL-*.txt`. Gotcha: Qt resources in a
*static* library aren't reliably auto-initialized — the `.qrc` is compiled into
each **executable** target instead.

## Cross-platform & packaging

Target: **macOS (primary) + Linux/Ubuntu**. The codebase is already portable —
pure Qt6, no OS-specific code, and `QStandardPaths::AppDataLocation` resolves
the DB path per-platform (`~/Library/Application Support/zeitbenutzer` on macOS,
`~/.local/share/zeitbenutzer` on Linux). Bundled fonts keep rendering identical.

- **Build:** same `cmake -S . -B build` on both (macOS: `brew install qt cmake`).
- **Packaging:** macOS → `MACOSX_BUNDLE` + `macdeployqt` → `.app`/`.dmg` (later:
  codesign/notarize). Linux → AppImage via `linuxdeploy` (most portable), or Flatpak.
- **Keep portable:** no shelling out to OS tools, no hardcoded paths. Watch HiDPI
  and macOS trackpad scroll/gestures when building the calendar grid (module 3).

---

## Data model (SQLite as truth)

```sql
project(
  id, parent_id → project.id NULL,   -- arbitrary nesting
  name, color, archived, sort)

note(
  id, project_id → project.id,        -- every note belongs to a project
  title, body_md, created_at, updated_at)

-- A recurring task template (e.g. "water plants daily").
task_series(
  id, project_id → project.id,
  title, estimate_min,
  rrule,                              -- RFC 5545 RRULE
  anchor_due,                         -- due time-of-day template per occurrence
  until NULL)

task(
  id, project_id → project.id,
  series_id → task_series.id NULL,    -- NULL = one-off task
  occurrence_date NULL,               -- which slot in the series this fills
  title, status,                      -- todo | doing | done | cancelled
  estimate_min, due, completed_at, sort)

-- A recurring template. One-off blocks have no series.
block_series(
  id, project_id → project.id NULL,
  title, color,
  rrule,                              -- RFC 5545 RRULE, e.g. FREQ=WEEKLY;BYDAY=MO,WE,FR
  anchor_start, anchor_end,           -- time-of-day + duration template of occurrence
  until NULL)                         -- recurrence end (NULL = open-ended)

-- A concrete scheduled span. series_id NULL = one-off.
block(
  id, series_id → block_series.id NULL,
  occurrence_date NULL,               -- which slot in the series this fills
  planned_start, planned_end,         -- the PLAN
  actual_start NULL, actual_end NULL, -- the RECONCILE (NULL = unreconciled)
  project_id → project.id NULL,       -- canonical time attribution
  title, color, status,               -- planned | done | skipped | carried
  detached BOOL,                      -- instance edited away from its series
  skipped BOOL)                       -- this occurrence cancelled

block_task(block_id, task_id)         -- a block schedules tasks (many-to-many)
block_note(block_id, note_id)         -- a block references notes (many-to-many)

-- Module 6: read-only external calendars (overlay, never in rollups).
external_source(
  id, kind,                           -- 0 = file, 1 = url
  location,                           -- path or ICS URL (incl. secret iCal address)
  name, color, enabled, last_synced NULL)

external_event(                       -- cached, recurrence-expanded instances
  id, source_id → external_source.id,
  uid, summary, location_txt,
  start_utc, end_utc, all_day BOOL,
  adopted_block_id → block.id NULL)   -- set once adopted; overlay copy suppressed
```

### Design decisions baked into the schema

1. **Time is attributed to a project via `block.project_id`, not through its
   tasks.** A block is "about" one project for rollup purposes; tasks inside it
   are what you check off. "Hours on project X" stays a clean `SUM` instead of
   fanning out through tasks. A block may still carry tasks from any project.

2. **`actual_*` nullable is the reconcile state machine.** `actual_start IS NULL`
   = planned but not yet confirmed. Reconciling sets actuals (= planned, ≠
   planned, or skipped). This doubles as the time-tracking history for free.

3. **Recurrence via lazy materialization** (blocks *and* tasks). A
   `block_series` / `task_series` carries an RRULE and an occurrence template.
   For any visible window, occurrences are *expanded on the fly* from the series
   and rendered as phantom blocks/tasks. A real `block`/`task` row is persisted
   **only** when an occurrence is acted on — reconciled, completed, edited
   (`detached`), or skipped. This keeps an open-ended daily routine from
   exploding the tables to infinity while still allowing per-instance overrides.
   Each materialized occurrence is handled independently (its own `actual_*` /
   `status`).

### The bidirectional rollup (the payoff)

- **Block → "what is this":** join to its project, its tasks, its notes.
- **Project → "time on me":**
  - actual: `SUM(actual_end − actual_start) WHERE project_id = ? AND actual_start NOT NULL`
  - planned: same over `planned_*` (plus series expansion for future horizon).
  - slice by week trivially; recurse `parent_id` to roll subprojects into parents.

---

## UI layout (three panes)

```
┌────────────┬───────────────────────────────┬──────────────┐
│ PROJECT    │   CALENDAR  (day / week)      │  INSPECTOR    │
│ TREE       │                               │               │
│            │   08 ┌─────────┐              │  selected     │
│ ▾ zero_kel │   09 │worldgen │ ← block      │  block, OR    │
│   ▸ world  │   10 └─────────┘              │  today's      │
│   ▸ render │   11 ┌─────────┐              │  TASK POOL    │
│ ▾ ur-rdr   │   12 │ lunch ↻ │ ← recurring  │  (drag onto   │
│ ▸ skein2   │   ...                         │   the grid →) │
└────────────┴───────────────────────────────┴──────────────┘
```

- **Center — calendar grid** (`QGraphicsView`): drag-to-create, drag-edges-to-
  resize, snap-to-15min. **Plan-vs-actual = split column:** each day is split
  into a *planned* sub-column and an *actual* sub-column side by side, so a
  block's intention and its outcome sit next to each other rather than
  overlapping. Recurring occurrences carry a ↻ marker. Center pane toggles
  between Calendar and a selected Project's detail view.
- **Left — project tree** (`QTreeView` + model): create/nest/reorder projects.
  Selecting a project opens its detail (notes + tasks + time rollup) in center.
- **Right — context pane:** inspector when a block is selected; otherwise the
  **unscheduled task pool** — drag a task onto the grid to create a linked block.
- **Notes** edit in a markdown editor (`QTextEdit::setMarkdown`) within the
  project detail view.

## The daily loop (the product)

1. **Morning — plan.** Pull tasks from the pool, drag them into time blocks.
   Pure intention; `actual_*` stays NULL.
2. **During the day — nudge.** Mark a block done as you go, or adjust it.
3. **Evening — reconcile.** A review mode walks unreconciled blocks:
   *happened as planned / happened differently / skipped / carry over.* Setting
   actuals populates time history and the project rollups.

---

## Build phases (per-module, incremental — ur-reader style)

| # | Module | Delivers | Notes |
|---|--------|----------|-------|
| 0 | **storage** | SQLite schema, migrations, model/DAO classes. No UI. | Fully unit-tested. Recurrence expansion logic lives here. |
| 1 | **projects + notes** | Project tree (create/nest), markdown notes per project. | Usable as a plain note app already — a skein replacement. |
| 2 | **tasks** | Task type, task pool, statuses. | Still no calendar. |
| 3 | **calendar (plan)** | Day/week grid, create/move/resize blocks, link to project + tasks. **Recurring blocks** (series, RRULE, phantom occurrences). | The big visual module. |
| 4 | **reconcile** | `actual_*`, evening review flow, plan-vs-actual overlay, per-occurrence edit/skip/detach. | Closes the loop. |
| 5 | **rollups** | Project detail: planned/actual time, week/all-time, recursing subprojects. | Payoff of the relational model. |
| 6 | **external calendars** | Read-only overlay of `.ics` file / ICS-URL sources (Google/iCloud/Outlook via secret iCal address, no OAuth). All-day band + timed overlay; right-click to adopt into a real block. | Dependency-free parser; cached, never in rollups. |
| 7+ | **later** | Day templates ("a typical Tuesday"), optionally skein's embeddings/Claude features. | Parked. |

Recurrence is front-loaded into module 3 (not deferred) because retrofitting a
series/occurrence split onto a flat block table is the expensive kind of change.

---

## Resolved behaviors

- **Recurring tasks:** in v1 (mirrors recurring blocks via `task_series`).
- **Plan-vs-actual rendering:** split column per day (planned | actual).
- **Carry-over:** carrying a block **clones** it onto a chosen day (default
  tomorrow). The original stays put on its day, marked `status = carried`, so
  scrolling back still shows that the work was planned (and unfinished) there.
  The clone is a fresh block, linked to the same project/tasks/notes, and
  records `carried_from` = the origin id for an audit trail.
- **SQLite access:** Qt's bundled `QSQLITE` driver (`Qt6::Sql`), not a direct
  `libsqlite3` link — keeps the dependency set to Qt alone. (Module 0.)
- **External calendars:** read-only **overlay**, not imported as blocks. Cached
  in `external_source` / `external_event` and surfaced via a separate query, so
  they never touch reconcile or rollups. Recurrence is expanded at fetch time
  into concrete instances (foreign RRULEs aren't round-tripped through our
  subset). "Adopt" creates a real block and suppresses the overlay copy; the
  link survives re-sync (matched on `uid`). Google support is "paste your secret
  iCal URL" — no OAuth / native API. Parser is best-effort: `VTIMEZONE`,
  `RDATE`, and `RECURRENCE-ID` overrides are skipped rather than erroring.

## Open questions (not yet decided)

- Nothing currently open for modules 0–6.
