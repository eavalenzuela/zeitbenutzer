#pragma once

// Centralized look-and-feel. A Theme is a small palette; the global QSS and the
// markdown-preview CSS are generated from it, so adding a dark theme later is
// just another Theme + a call to setCurrentTheme(). Fonts are handled separately
// (typography.h) and intentionally not set here.

#include <QColor>
#include <QString>

class QApplication;
class QTextBrowser;

namespace zb {

struct Theme {
    QString name;
    QColor  window;      // app / splitter background
    QColor  panel;       // tree / list / input background
    QColor  border;      // panel + input borders
    QColor  text;        // primary text
    QColor  subtleText;  // secondary text
    QColor  accent;      // selection / highlight
    QColor  accentText;  // text drawn on accent
    QColor  hover;       // toolbar button hover
    QColor  codeBg;      // inline-code / preformatted background
};

Theme lightTheme();

// The active theme (defaults to lightTheme()). setCurrentTheme() is the hook
// for future theme switching.
const Theme& currentTheme();
void         setCurrentTheme(const Theme& t);

// Generate the application-wide stylesheet for a theme.
QString buildStyleSheet(const Theme& t);

// Apply a theme app-wide (sets it current + installs the stylesheet).
void applyTheme(QApplication& app, const Theme& t);

// Install the markdown-preview document CSS: inline code / code blocks render
// in the monospace editor font, links/headings pick up theme colors.
void stylePreview(QTextBrowser* browser, const Theme& t);

} // namespace zb
