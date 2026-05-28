#include "app/theme.h"

#include "app/typography.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QTextBrowser>
#include <QTextDocument>
#include <cmath>

namespace zb {

namespace {
Theme g_current = lightTheme();
}

Theme lightTheme()
{
    Theme t;
    t.name       = QStringLiteral("light");
    t.window     = QColor(0xf5, 0xf6, 0xf8);
    t.panel      = QColor(0xff, 0xff, 0xff);
    t.border     = QColor(0xdf, 0xe1, 0xe6);
    t.text       = QColor(0x1f, 0x23, 0x28);
    t.subtleText = QColor(0x6b, 0x72, 0x80);
    t.accent     = QColor(0x25, 0x63, 0xeb);
    t.accentText = QColor(0xff, 0xff, 0xff);
    t.hover      = QColor(0xe9, 0xeb, 0xef);
    t.codeBg     = QColor(0xf0, 0xf1, 0xf4);
    t.quoteBg    = QColor(0xf0, 0xf1, 0xf4);
    t.synKeyword = QColor(0xd7, 0x3a, 0x49);
    t.synString  = QColor(0x03, 0x2f, 0x62);
    t.synNumber  = QColor(0x00, 0x5c, 0xc5);
    t.synComment = QColor(0x6a, 0x73, 0x7d);
    return t;
}

Theme darkTheme()
{
    Theme t;
    t.name       = QStringLiteral("dark");
    t.window     = QColor(0x1e, 0x1f, 0x22);
    t.panel      = QColor(0x2b, 0x2d, 0x30);
    t.border     = QColor(0x3a, 0x3d, 0x41);
    t.text       = QColor(0xe6, 0xe6, 0xe6);
    t.subtleText = QColor(0x9a, 0xa0, 0xa6);
    t.accent     = QColor(0x4f, 0x8c, 0xff);
    t.accentText = QColor(0xff, 0xff, 0xff);
    t.hover      = QColor(0x34, 0x37, 0x3b);
    t.codeBg     = QColor(0x34, 0x37, 0x3b);
    t.quoteBg    = QColor(0x2f, 0x31, 0x35);
    t.synKeyword = QColor(0xff, 0x7b, 0x72);
    t.synString  = QColor(0xa5, 0xd6, 0xff);
    t.synNumber  = QColor(0x79, 0xc0, 0xff);
    t.synComment = QColor(0x8b, 0x94, 0x9e);
    return t;
}

Theme themeByName(const QString& name)
{
    return name == QStringLiteral("dark") ? darkTheme() : lightTheme();
}

const Theme& currentTheme() { return g_current; }
void setCurrentTheme(const Theme& t) { g_current = t; }

QColor projectColor(const QString& explicitHex, qint64 id)
{
    if (!explicitHex.isEmpty()) {
        const QColor c(explicitHex);
        if (c.isValid())
            return c;
    }
    // Golden-angle hue spread gives well-separated colors across ids.
    const double hue = std::fmod(static_cast<double>(id) * 137.508, 360.0);
    return QColor::fromHsl(static_cast<int>(hue), 150, 145);
}

QString buildStyleSheet(const Theme& t)
{
    const QString window = t.window.name();
    const QString panel  = t.panel.name();
    const QString border = t.border.name();
    const QString text   = t.text.name();
    const QString accent = t.accent.name();
    const QString accentText = t.accentText.name();
    const QString hover  = t.hover.name();

    return QString(QLatin1String(R"(
QWidget { color: %1; }
QMainWindow, QSplitter { background: %2; }

QToolBar { background: %2; border: none; padding: 4px; spacing: 2px; }
QToolBar QToolButton { padding: 4px 10px; border-radius: 5px; color: %1; }
QToolBar QToolButton:hover { background: %6; }
QToolBar QToolButton:pressed { background: %3; }

QTreeView, QListWidget {
    background: %4; border: 1px solid %3; border-radius: 6px;
    padding: 4px; outline: 0;
}
QTreeView::item, QListWidget::item { padding: 4px 6px; min-height: 22px; border-radius: 4px; }
QTreeView::item:selected, QListWidget::item:selected { background: %5; color: %7; }
QTreeView::item:hover, QListWidget::item:hover { background: %6; }

QLineEdit, QPlainTextEdit, QTextBrowser {
    background: %4; border: 1px solid %3; border-radius: 6px; padding: 6px 8px;
    selection-background-color: %5; selection-color: %7;
}
QLineEdit:focus, QPlainTextEdit:focus { border: 1px solid %5; }

/* Inline rename editor inside tree/list views: tight padding so the text
   fits the row height instead of being clipped by the input padding above. */
QAbstractItemView QLineEdit {
    padding: 1px 3px; border: 1px solid %5; border-radius: 3px;
}

QTabBar::tab {
    padding: 5px 12px; margin-right: 2px;
    border-top-left-radius: 5px; border-top-right-radius: 5px; color: %1;
}
QTabBar::tab:selected { background: %4; }
QTabWidget::pane { border: 1px solid %3; border-radius: 6px; top: -1px; }

QSplitter::handle { background: %2; }
QSplitter::handle:horizontal { width: 6px; }
QSplitter::handle:vertical { height: 6px; }
)"))
        .arg(text, window, border, panel, accent, hover, accentText);
}

void applyTheme(QApplication& app, const Theme& t)
{
    setCurrentTheme(t);

    // Fusion + a palette gives consistent theming across every widget (combos,
    // menus, dialogs, scrollbars) and across platforms — native styles ignore
    // the palette, so dark mode wouldn't take without this.
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette pal;
    pal.setColor(QPalette::Window, t.window);
    pal.setColor(QPalette::Base, t.panel);
    pal.setColor(QPalette::AlternateBase, t.window);
    pal.setColor(QPalette::WindowText, t.text);
    pal.setColor(QPalette::Text, t.text);
    pal.setColor(QPalette::Button, t.window);
    pal.setColor(QPalette::ButtonText, t.text);
    pal.setColor(QPalette::ToolTipBase, t.panel);
    pal.setColor(QPalette::ToolTipText, t.text);
    pal.setColor(QPalette::PlaceholderText, t.subtleText);
    pal.setColor(QPalette::Highlight, t.accent);
    pal.setColor(QPalette::HighlightedText, t.accentText);
    pal.setColor(QPalette::Link, t.accent);
    pal.setColor(QPalette::Disabled, QPalette::Text, t.subtleText);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, t.subtleText);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, t.subtleText);
    app.setPalette(pal);

    app.setStyleSheet(buildStyleSheet(t));
}

void stylePreview(QTextBrowser* browser, const Theme& t)
{
    if (!browser)
        return;
    const QString css = QString(QLatin1String(
        "code, pre { font-family: '%1'; background-color: %2; }"
        "h1, h2, h3, h4 { color: %3; }"
        "a { color: %4; }"))
        .arg(editorFont().family(), t.codeBg.name(), t.text.name(),
             t.accent.name());
    browser->document()->setDefaultStyleSheet(css);
}

} // namespace zb
