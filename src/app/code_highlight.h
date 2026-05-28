#pragma once

// Hand-rolled syntax highlighting for fenced code blocks (Phase 3). One generic
// lexer driven by a small per-language spec (keywords, comment + string styles)
// covers the languages we care about: Python, shell, JS/TS/Node, JSON, YAML.
//
// Returns inner HTML for a <pre>: HTML-escaped code with keyword/string/number/
// comment runs wrapped in themed <span> colors. An unknown or empty language
// yields escaped plain text — the code still renders verbatim, just uncolored.

#include <QString>

namespace zb {

struct Theme;

QString highlightCode(const QString& code, const QString& lang, const Theme& theme);

} // namespace zb
