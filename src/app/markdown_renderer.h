#pragma once

// Single owner of the note markdown → HTML conversion used by the preview pane.
// Replaces direct QTextBrowser::setMarkdown so we can grow our own flavor
// (task checkboxes, :::csv tables, images, [[wikilinks]], …) behind one seam.
//
// The conversion is staged as a pipeline:
//   segment      — lift our custom blocks out into placeholders   [Phase 4+]
//   renderStd    — the CommonMark/GFM 90%, via Qt for now         [Phase 1]
//   splice       — substitute generated HTML back for placeholders[Phase 4+]
//   postProcess  — checkboxes, image src, wikilink anchors        [Phase 2/5/6]
//
// Phase 1 wires only the seam: segmentation/splice/post-process are no-ops, the
// standard markdown is rendered by Qt, and the output is visually identical to
// the previous setMarkdown path. See markdown_enrichment.md.

#include <QString>

#include "storage/types.h"

namespace zb {

class Store;

// What the post-processing stages need. `store` (when set) enables `[[wikilink]]`
// resolution against note titles; nullptr leaves wikilinks as literal text.
struct RenderContext {
    Id     noteId = -1;
    Store* store = nullptr;
};

namespace MarkdownRenderer {

QString toHtml(const QString& markdown, const RenderContext& ctx = {});

} // namespace MarkdownRenderer

} // namespace zb
