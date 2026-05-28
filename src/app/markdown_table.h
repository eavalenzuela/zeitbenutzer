#pragma once

// Renders a :::csv / :::tsv block (our custom table flavor) to an HTML <table>.
// See markdown_enrichment.md, Deep Dive A. The renderer (markdown_renderer.cpp)
// lifts these fences out during segmentation and calls this to build the table.
//
//   info         text after the fence tag, e.g. "noheader delim=; Q1 Budget"
//   defaultDelim ',' for :::csv, '\t' for :::tsv (info may override via delim=)
//   body         the raw row lines, newline-joined
//
// Empty / no-row input yields an empty string (the block renders nothing).

#include <QString>

namespace zb {

struct Theme;

QString renderCsvTable(const QString& info, QChar defaultDelim,
                       const QString& body, const Theme& theme);

} // namespace zb
