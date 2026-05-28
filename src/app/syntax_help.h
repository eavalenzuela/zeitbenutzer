#pragma once

// The Markdown syntax reference panel: a single shared, non-modal, scrollable
// window documenting exactly what the note renderer supports (Phases 1-6).
// Opened from the editor's info icon and from Help → Markdown Syntax…

class QWidget;

namespace zb {

// Show the panel, or raise it if already open.
void showSyntaxHelp(QWidget* parent);

} // namespace zb
