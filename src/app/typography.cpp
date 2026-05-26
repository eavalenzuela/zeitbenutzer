#include "app/typography.h"

#include <QFontDatabase>
#include <QString>
#include <QStringList>

namespace zb {

namespace {

// Resolved family names, captured from the loaded fonts so we never guess the
// family string. Empty until registerBundledFonts() runs.
QString g_sansFamily;
QString g_monoFamily;
bool    g_loaded = false;

// Load one embedded TTF; return its first family name (or empty on failure).
QString loadFont(const QString& path)
{
    const int id = QFontDatabase::addApplicationFont(path);
    if (id < 0)
        return {};
    const QStringList fams = QFontDatabase::applicationFontFamilies(id);
    return fams.isEmpty() ? QString() : fams.first();
}

} // namespace

void registerBundledFonts()
{
    if (g_loaded)
        return;
    g_loaded = true;

    // Regular faces define the family name; the others register additional
    // weights/styles under the same family so bold/italic resolve to real cuts.
    g_monoFamily = loadFont(QStringLiteral(":/fonts/SourceCodePro-Regular.ttf"));
    loadFont(QStringLiteral(":/fonts/SourceCodePro-Bold.ttf"));

    g_sansFamily = loadFont(QStringLiteral(":/fonts/SourceSans3-Regular.ttf"));
    loadFont(QStringLiteral(":/fonts/SourceSans3-Bold.ttf"));
    loadFont(QStringLiteral(":/fonts/SourceSans3-It.ttf"));
    loadFont(QStringLiteral(":/fonts/SourceSans3-BoldIt.ttf"));
}

QFont uiFont()
{
    // Fall back to the platform sans if loading somehow failed.
    QFont f(g_sansFamily.isEmpty() ? QStringLiteral("sans-serif") : g_sansFamily);
    f.setPointSize(11);
    return f;
}

QFont editorFont()
{
    QFont f(g_monoFamily.isEmpty() ? QStringLiteral("monospace") : g_monoFamily);
    f.setPointSize(11);
    f.setFixedPitch(true);
    return f;
}

} // namespace zb
