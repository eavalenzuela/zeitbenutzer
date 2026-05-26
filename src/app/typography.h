#pragma once

// Bundled-font registration so the app renders identically on every platform,
// regardless of installed system fonts. Fonts are embedded as Qt resources
// (see assets/fonts.qrc) and loaded at startup.
//
//   UI / preview prose : Source Sans 3   (OFL 1.1)
//   editor body / code : Source Code Pro (OFL 1.1)

#include <QFont>

namespace zb {

// Load the embedded fonts into the application font database. Call once after
// the QApplication is constructed, before creating windows. Idempotent.
void registerBundledFonts();

// The proportional UI font (chrome + markdown preview).
QFont uiFont();

// The monospace editor font (note body, code).
QFont editorFont();

} // namespace zb
