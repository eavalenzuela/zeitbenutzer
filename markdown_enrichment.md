# Markdown Enrichment — Design & Scoping

Working doc for enriching zeitbenutzer's note markdown beyond Qt's built-in
`setMarkdown`. This is the shared reference; deep-dive sections (images, table
grammar) live here too. Decisions captured 2026-05-27.

## Philosophy fit

DESIGN.md commits us to: **Qt Widgets, no webview shell**, **no extra
dependencies** where avoidable, and **a single SQLite file you can back up**.
Every choice below is constrained by those three. In particular we do *not*
adopt QWebEngine (≈150 MB Chromium, contradicts "no webview"); we keep
`QTextBrowser` and feed it HTML we generate ourselves.

> `SQLite owns the structure; markdown owns only prose.` — we stay inside that
> line. The enrichments add *prose richness* (tables, images, code, links), not
> structured data that belongs in the schema. The one schema addition (images)
> is binary content, not structure.

---

## Current state (baseline)

| Concern | Where | Notes |
|---|---|---|
| Note body storage | `note.body_md` TEXT — `src/storage/database.cpp:31-38` | raw markdown, plain text |
| Note struct | `Note::bodyMd` — `src/storage/types.h:31-38` | |
| Store API | `src/storage/store.h:28-33` | `note()`, `updateNote()`, etc. |
| Editor + preview | `src/app/note_editor.cpp` | `QPlainTextEdit` (raw) ‖ `QTextBrowser` (preview) split |
| Render call | `note_editor.cpp:74,84,109` | `m_preview->setMarkdown(...)` — **the seam we replace** |
| Preview CSS | `stylePreview()` — `src/app/theme.cpp:154-165` | themes `code/pre`, `h1-h4`, `a` only |
| Schema/migrations | `database.cpp:18` `kSchemaVersion=2`; `migrate()` at `:193` | add a branch + bump to evolve |

**What renders today** (Qt's md4c, ~CommonMark): headings, bold/italic, lists,
inline code, fenced code (unhighlighted), links, blockquotes, hr.
**Missing**: task checkboxes, tables, images, syntax highlighting, strikethrough,
wikilinks.

`QTextBrowser`'s HTML subset *does* support `<table>`, `<img>`, `<pre>`, and
inline `style=` — which is exactly why owning the HTML output unlocks
everything below without a webview.

---

## Architecture: the converter seam

Introduce one chokepoint and route the preview through it:

```
m_preview->setMarkdown(md)   →   m_preview->setHtml(MarkdownRenderer::toHtml(md, ctx))
```

`MarkdownRenderer` (new, in `zb-app`) is the single owner of markdown→HTML.
Internally it runs a **pipeline**:

```
raw markdown
  → [segmentation pass]   lift our custom blocks (:::csv:::, code fences we want
                          to own) into placeholders, remember their payloads
  → [standard markdown]   convert the remaining 90% (headings/lists/emphasis/…)
  → [splice]              replace placeholders with our generated HTML
  → [post-process]        checkboxes, wikilink anchors, image src rewriting
  → HTML string
```

Why segmentation rather than forking a parser: it lets us define *our own*
flavor (CSV tables, owned code fences) without maintaining a patched grammar,
and it's parser-agnostic for the standard 90%.

**Standard-markdown engine choice (open):** simplest first step is to keep
calling Qt to render the non-custom segments (`QTextDocument` →
`setMarkdown` → `toHtml`) so Phase 1 is a no-behavior-change refactor. If Qt's
HTML proves too lossy for splicing, vendor md4c (tiny C, already what Qt uses)
and emit HTML ourselves. Decide empirically in Phase 1.

`ctx` carries what post-processing needs: the current note id (for image
resolution + relative wikilinks) and the theme (so highlight colors and table
borders match light/dark).

### Rendering surface limits to respect
- Qt rich text CSS is a small subset (no flexbox, limited selectors). Style via
  inline attributes / simple element selectors, extend `stylePreview()`.
- No interactive widgets in the document → checkboxes are glyphs, not controls.
- Images load via `QTextBrowser::loadResource()` override (Phase 5).
- In-app links need `anchorClicked` handling (external links already open via
  `setOpenExternalLinks(true)`, `note_editor.cpp:31`).

---

## Phased plan

Ordered by smallest blast radius first. Each phase ships independently.

### Phase 1 — Converter seam (foundation, no visible change)
**Goal:** all preview rendering goes through `MarkdownRenderer::toHtml`, output
visually identical to today.
- New `src/app/markdown_renderer.{h,cpp}`, added to `zb-app` in `CMakeLists.txt`.
- Replace the three `setMarkdown` call sites (`note_editor.cpp:74,84,109`) with
  `setHtml(MarkdownRenderer::toHtml(...))`.
- Pipeline skeleton with empty segmentation/post-process stages.
- Smoke test: render a fixture note, assert key HTML elements present.
**Risk:** Qt markdown→HTML fidelity. Mitigation: compare before/after on sample
notes; fall back to vendored md4c if needed.

### Phase 2 — Checkboxes ✅
**Goal:** `- [ ]` / `- [x]` render as ☐ / ☑.
- **Finding:** Qt's GitHub dialect already parses task lists, emitting
  `<li class="unchecked/checked">` with `::marker` glyphs, and they survive the
  Phase 1 `toHtml→setHtml` round-trip as real `Unchecked`/`Checked` block
  markers. So rendering came for free with the seam.
- **What we did:** the only gap was the glyph — Qt uses ☒ (U+2612) for done; we
  prefer ☑ (U+2611). The renderer's `postProcess` stage swaps that one glyph
  (`applyCheckboxGlyph`, the first real use of the post-process stage). Source
  text remains the toggle mechanism; no widget, no click handler.
- Optional later: click-to-toggle by mapping preview position → source line.
**Done:** `markdown_renderer.cpp` postProcess + smoke checks (markers survive the
seam; done glyph is ☑).

### Phase 3 — Syntax highlighting (hand-rolled)
**Goal:** fenced code blocks colored per language.
- Owned fence handling in segmentation: ```` ```lang ```` → highlighted `<pre>`.
- Per-language tokenizers emitting inline `<span style="color:…">`. Colors from
  theme so they read in light/dark.
- **Target languages:** Python, shell, JavaScript/TypeScript/Node (one shared
  JS-family lexer), JSON, YAML/config.
- Unknown/absent lang → plain themed `<pre>` (today's behavior).
- Structure: a `Highlighter` interface + small per-language lexers; a registry
  keyed by fence info-string (with aliases: `js`/`ts`/`node`, `sh`/`bash`/`zsh`,
  `yml`/`yaml`).
**Scope:** medium; additive per language.

### Phase 4 — CSV/TSV tables (custom flavor) → see Deep Dive A
**Goal:** `:::csv … :::` (and `:::tsv`) fenced blocks render as `<table>`.
**Scope:** medium; lives entirely in the segmentation + a table-HTML emitter.

### Phase 5 — Images → see Deep Dive B
**Goal:** embedded images render; storage backend selectable; orphans collectible.
**Scope:** largest — touches schema (v3), store API, Settings, renderer,
`loadResource`, and a GC path.

### Phase 6 — Wikilinks `[[…]]` + completion
**Goal:** `[[Note title]]` becomes an in-app link; `[[` triggers completion.
- Render: post-process `[[target]]` → `<a href="zb://note/ID">`. Resolve target
  (by title, maybe `Project/Note`) to an id at render time via `ctx`.
- Navigate: handle `anchorClicked` for the `zb://` scheme (don't open browser);
  wire to load that note in the editor.
- Completion: `QCompleter` on `m_body`, popup on `[[`, model = note titles +
  project tree from SQLite. Insert resolves ambiguity (dup titles → disambiguate
  by project path).
**Scope:** medium; touches editor UI + a small navigation signal.

---

## Deep Dive A — CSV/TSV table grammar (resolved 2026-05-27)

A `:::` container fence (distinct from ` ``` ` code fences), lifted out by the
segmentation pass before the standard markdown engine ever sees it.

```
:::csv noheader delim=; Q1 Budget
Design,Ed,done
Build,Ed,wip
:::
```

### Fence info-line grammar
```
:::(csv|tsv) [options...] [caption text]
```
Tokenize the text after `:::csv`/`:::tsv` by spaces. Consume leading tokens
**while** each is either the flag `noheader` or a `key=value` pair. The first
token that is neither ends the options; the remainder of the line (original
spacing preserved) is the **caption** → `<caption>`.

| Fence | Result |
|---|---|
| `:::csv` | comma, header on, no caption |
| `:::csv noheader` | comma, header off |
| `:::csv delim=;` | semicolon, header on |
| `:::csv noheader delim=; Q1 Budget` | semicolon, header off, caption "Q1 Budget" |
| `:::csv Q1 Budget` | comma, header on, caption "Q1 Budget" |

- **`csv` vs `tsv`:** sets the default delimiter (comma vs tab).
- **`delim=X`:** override delimiter with a single char (e.g. `delim=;` for
  European spreadsheets). Overrides the csv/tsv default.
- **`noheader`:** all rows are `<td>` (plain grid). Default: first row is `<th>`.
- **Known limit:** a caption whose first word is literally `noheader` or matches
  `x=y` gets consumed as an option. Documented, not escaped.

### Cell parsing
- **Quoting:** RFC-4180. A field wrapped in `"…"` may contain the delimiter;
  `""` inside a quoted field is a literal `"`. **No multi-line quoted fields**
  (fence termination is purely line-based — a lone `:::` always closes), so a
  cell cannot contain a newline. Documented constraint.
- **Whitespace:** unquoted cells are trimmed; quoted cells preserve spaces.
- **Markdown inside cells:** each cell's text is run through an inline-markdown
  renderer (bold/italic/code/links — the same inline pass Phase 6 wikilinks
  reuse). Note: CSV splitting happens *before* inline parsing, so a cell whose
  content contains the delimiter (e.g. a link `[a](x,y)` under comma delim) must
  be quoted.
- **Ragged rows:** column count = widest row; short rows padded with empty
  cells. Never truncate (lossless).

### Rendering
- **Alignment:** auto right-align a column when *every* data cell in it parses
  as a number (detected on raw cell text, so `**42**` → non-numeric → left);
  all other columns left-aligned.
- **Emit:** Qt-subset `<table>` with optional `<caption>`, `<thead>` (unless
  `noheader`) + `<tbody>`. Borders + bold header (+ subtle header background)
  via `stylePreview()` (`theme.cpp:154`). **Zebra striping is NOT promised** —
  Qt rich-text CSS support for per-row backgrounds is unreliable.
- **Malformed / empty:** no closing `:::` → not treated as a table at all; text
  passes through to the standard markdown engine (keeps live preview sane while
  typing). Empty block → renders nothing.

_Implementation lives entirely in the segmentation stage + a table-HTML emitter;
the inline-cell renderer is shared with Phase 6._

---

## Deep Dive B — Images (resolved 2026-05-27)

**Storage:** a **Settings option**, default = **blobs in SQLite** (keeps the
single-file-backup promise); alternative = files in an `attachments/` dir beside
the `.db`. The store abstracts both behind one lookup by row id.

### Schema (v3)
Add a branch in `migrate()` (`database.cpp:193`) and bump `kSchemaVersion` to 3:
```sql
CREATE TABLE image (
    id         INTEGER PRIMARY KEY,
    sha256     TEXT    NOT NULL UNIQUE,  -- dedup key (hidden from the body)
    mime       TEXT    NOT NULL,
    bytes      BLOB,                     -- NULL when stored on disk
    path       TEXT,                     -- relative path when stored on disk
    created_at INTEGER NOT NULL
);
```
Exactly one of `bytes`/`path` is populated per row, per the active backend.

### In-body reference
Standard markdown image syntax with a custom URL scheme, referencing the **row
id** (clean in prose); the sha256 stays hidden as the dedup key:
```
![alt](zb-img:42)
![alt](zb-img:42?maxwidth=400)
```
- **Why id, not sha:** short and readable in the raw markdown. The body only
  resolves within this DB anyway (images live in it / beside it), so
  content-addressing in the body buys nothing.
- **`?maxwidth=N`:** optional query param. A literal space form
  (`zb-img:42 maxwidth=400`) can't be used — in markdown a space in the `(...)`
  destination starts a *title*. The query form keeps it one valid URL so images
  need **no post-processing stage** — they ride Qt's normal `<img>` path.
- Trivially extensible to `?maxheight=N` later.

### Insertion (all three in v1)
- **Clipboard paste:** override `insertFromMimeData`/`canInsertFromMimeData` on
  the body editor (screenshots — highest value).
- **Drag-and-drop:** accept image files dropped on the editor.
- **File picker:** a toolbar/menu action opening a file dialog.
- On insert: read bytes → compute sha256 → `SELECT id WHERE sha256=?`; reuse if
  present, else `INSERT` (into blob or disk per backend) → insert
  `![alt](zb-img:ID)` at the cursor. **Stored original as-is** (no downscale /
  re-encode on import).

### Rendering
- `QTextBrowser::loadResource()` override: on a `zb-img:` URL, parse the id (and
  `?maxwidth`), fetch bytes (blob) or read the file (disk), decode to `QImage`.
- **Scaling:** `?maxwidth=N` → `scaledToWidth(N)` only when wider (aspect ratio
  preserved, linear). No param → cap to the preview viewport width so large
  pastes don't overflow.
- **Cache (required):** the preview re-renders every keystroke
  (`note_editor.cpp:84`), so cache decoded images keyed on `(id, maxwidth)` to
  keep typing near an image smooth.
- **Formats:** whatever `QImage` reads (PNG/JPEG/GIF/WebP); store original bytes
  + MIME. SVG deferred (needs the Qt SVG module).

### Backend switch (Settings)
Toggling blob↔disk **eagerly migrates** all existing images in one transaction
(blob→disk: write files, null `bytes`; disk→blob: read files into `bytes`,
remove files), with a confirm + simple progress. The store is always internally
consistent afterward.

### Orphan GC
Images are referenced *only* from note bodies, so an `image` row referenced by
no body is garbage. **Mark-and-sweep**: scan all `note.body_md` for `zb-img:ID`
tokens; delete `image` rows (and their files) whose id appears in none.
- **Trigger:** a manual "Reclaim space" action in Settings, **plus** an
  automatic sweep on app launch/close. No per-save cost; simple and correct.

_Largest phase — touches schema v3, store API, Settings, the editor (insertion),
`loadResource`, and the GC path._

---

## Cross-cutting feature — in-editor syntax help

An `i`-in-circle info icon in the note editor opens a **scrollable, sectioned
cheat-sheet** of the supported markdown syntax. Decided 2026-05-27.
- **Trigger/surface:** click opens a small non-modal, frameless popup panel
  (not a hover tooltip — the reference is too big to live in a tooltip). Stays
  open until dismissed; scrolls.
- **Second entry point:** a new `Help` menu in the menu bar (the app currently
  has only `&App` — Settings/Calendars/Quit, `main_window.cpp:63-80`) with a
  `Markdown Syntax…` action that opens the *same* panel widget. One content
  source, two doors. (A generic "Docs" entry is deferred until there's broader
  app documentation to justify it; `About` can join `Help` later too.)
- **Icon:** `QStyle::SP_MessageBoxInformation` (no bundled asset), as a flat
  `QToolButton` placed next to the title field in `note_editor.cpp` (the title
  row becomes a small `QHBoxLayout`).
- **Content:** sectioned — Text, Lists, Tasks, Code (with the supported langs),
  Tables (`:::csv` grammar + options), Images, Links. **Maintained per phase:**
  each enrichment phase appends its section when it lands, so the panel only
  ever documents syntax that actually works.
- **Timing:** build *after* some enrichments exist (deferred — not Phase 1).
  Natural fit alongside Phase 4/5 when the syntax is rich enough to need it.

## Cross-cutting open questions
- Standard-markdown engine: lean on Qt vs. vendor md4c (decide in Phase 1).
- Theming: extend `stylePreview()` (`theme.cpp:154`) for tables, checkboxes,
  and syntax colors — one place, light/dark aware.
- Export/interop: tokens like `zb-img:` and `:::csv` are non-standard; if we ever
  export notes, need a flattening pass. Out of scope now; noted.

## Status
- [x] Phase 1 — converter seam (`markdown_renderer.{h,cpp}`; preview routes
      through `MarkdownRenderer::toHtml`; Qt-engine standard render; smoke-tested)
- [x] Phase 2 — checkboxes (Qt renders task lists; postProcess swaps ☒→☑; tested)
- [ ] Phase 3 — syntax highlighting
- [ ] Phase 4 — CSV/TSV tables
- [ ] Phase 5 — images
- [ ] Phase 6 — wikilinks + completion
- [ ] Cross-cutting — in-editor syntax help panel (deferred; content grows per phase)
