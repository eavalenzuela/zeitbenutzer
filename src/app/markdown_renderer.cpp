#include "app/markdown_renderer.h"

#include "app/code_highlight.h"
#include "app/markdown_table.h"
#include "app/theme.h"
#include "app/typography.h"
#include "storage/store.h"

#include <QRegularExpression>
#include <QStringList>
#include <QTextDocument>
#include <QVector>

#include <utility>

namespace zb {

namespace {

// A fenced code block lifted out before the standard markdown pass.
struct CodeSeg {
    QString lang;
    QString code;
};

// Placeholder paragraph standing in for an extracted code block. Plain
// uppercase + digits so the markdown engine renders it as inert text in its own
// <p>, which splice() then swaps for our highlighted <pre>.
QString placeholder(int k)
{
    return QStringLiteral("ZBCODEBLOCK%1").arg(k);
}

// Phase 3 segmentation — pull fenced code blocks (``` or ~~~) out of the raw
// markdown so we render them ourselves (single <pre>, preserved indentation,
// syntax colors) instead of Qt's lossy per-line serialization. An unterminated
// fence is left as ordinary text, matching the "don't swallow while typing"
// rule used for tables.
QString extractCodeBlocks(const QString& md, QVector<CodeSeg>& segs)
{
    const QStringList lines = md.split(QLatin1Char('\n'));
    const int n = lines.size();
    QStringList out;

    // Parse a fence marker at the start of a line (≤3 leading spaces, ≥3 of the
    // same fence char). Returns the run length, or 0 if not a fence; sets *fc to
    // the fence char and *rest to the trailing text.
    const auto fence = [](const QString& line, QChar* fc, QString* rest) -> int {
        int lead = 0;
        while (lead < line.size() && lead < 3 && line[lead] == QLatin1Char(' '))
            ++lead;
        if (lead >= line.size())
            return 0;
        const QChar c = line[lead];
        if (c != QLatin1Char('`') && c != QLatin1Char('~'))
            return 0;
        int k = lead, len = 0;
        while (k < line.size() && line[k] == c) { ++k; ++len; }
        if (len < 3)
            return 0;
        if (fc)
            *fc = c;
        if (rest)
            *rest = line.mid(k).trimmed();
        return len;
    };

    int i = 0;
    while (i < n) {
        QChar openFc;
        QString info;
        const int openLen = fence(lines[i], &openFc, &info);
        if (openLen >= 3) {
            // Find a matching closing fence (same char, ≥ length, nothing else).
            int close = -1;
            for (int j = i + 1; j < n; ++j) {
                QChar fc;
                QString rest;
                const int len = fence(lines[j], &fc, &rest);
                if (len >= openLen && fc == openFc && rest.isEmpty()) {
                    close = j;
                    break;
                }
            }
            if (close >= 0) {
                CodeSeg seg;
                seg.lang = info.section(QLatin1Char(' '), 0, 0); // first token
                QStringList body;
                for (int b = i + 1; b < close; ++b)
                    body << lines[b];
                seg.code = body.join(QLatin1Char('\n'));
                // Surround the placeholder with blanks so it's its own paragraph.
                out << QString() << placeholder(segs.size()) << QString();
                segs.push_back(seg);
                i = close + 1;
                continue;
            }
            // Unterminated: fall through and treat this line as ordinary text.
        }
        out << lines[i];
        ++i;
    }
    return out.join(QLatin1Char('\n'));
}

// User preference: a single newline renders as a line break (note-app hard
// wrap), not a CommonMark soft break that joins lines with a space. Qt's parser
// has no toggle for this, so we approximate by appending a hard-break marker
// (two trailing spaces) to a line only when the next line is also non-blank —
// leaving blank-line paragraph breaks intact. Runs after code extraction, so
// fenced blocks (now placeholders surrounded by blanks) are never touched.
QString applyHardBreaks(const QString& md)
{
    QStringList lines = md.split(QLatin1Char('\n'));
    for (int i = 0; i + 1 < lines.size(); ++i) {
        const QString& cur = lines[i];
        if (cur.trimmed().isEmpty())             // blank line: leave it
            continue;
        if (lines[i + 1].trimmed().isEmpty())    // next is blank: paragraph break
            continue;
        if (cur.endsWith(QLatin1String("  "))    // already a hard break
            || cur.endsWith(QLatin1Char('\\')))
            continue;
        lines[i] += QLatin1String("  ");
    }
    return lines.join(QLatin1Char('\n'));
}

// Standard markdown → HTML via Qt's own parser/serializer (GitHub dialect, as
// QTextBrowser::setMarkdown defaults to), now operating on text whose code
// blocks have been replaced by placeholders.
QString renderStandardMarkdown(const QString& md)
{
    QTextDocument doc;
    doc.setMarkdown(md, QTextDocument::MarkdownDialectGitHub);
    return doc.toHtml();
}

// Substitute each placeholder paragraph with a single highlighted <pre>. The
// replacement text is inserted positionally (not via regex backrefs) so code
// containing "\1"-like sequences can't be misread as capture references.
QString spliceCodeBlocks(QString html, const QVector<CodeSeg>& segs, const Theme& th)
{
    const QString family = editorFont().family();
    for (int k = 0; k < segs.size(); ++k) {
        const QRegularExpression re(QStringLiteral("<p[^>]*>%1</p>").arg(placeholder(k)));
        const QRegularExpressionMatch m = re.match(html);
        if (!m.hasMatch())
            continue;
        const QString inner = highlightCode(segs[k].code, segs[k].lang, th);
        const QString pre = QStringLiteral(
            "<pre style=\"font-family:'%1'; background-color:%2; "
            "white-space:pre;\">%3</pre>")
            .arg(family, th.codeBg.name(), inner);
        html.replace(m.capturedStart(), m.capturedLength(), pre);
    }
    return html;
}

// A :::csv / :::tsv table block lifted out before the standard markdown pass.
struct TableSeg {
    QString info;  // text after the fence tag (options + caption)
    QChar   delim; // ',' for csv, '\t' for tsv (info may override)
    QString body;  // raw row lines, newline-joined
};

QString placeholderTable(int k)
{
    return QStringLiteral("ZBTABLE%1").arg(k);
}

// Does this line open a :::csv/:::tsv block? (≤3 leading spaces; the tag must be
// followed by a space or end-of-line so ":::csvfoo" isn't mistaken for one.)
bool tableOpen(const QString& line, QChar* delim, QString* info)
{
    int lead = 0;
    while (lead < line.size() && lead < 3 && line[lead] == QLatin1Char(' '))
        ++lead;
    const QStringView v = QStringView(line).mid(lead);
    const auto match = [&](QStringView tag, QChar d) {
        if (!v.startsWith(tag))
            return false;
        if (v.size() > tag.size() && !v.at(tag.size()).isSpace())
            return false;
        *delim = d;
        *info = v.mid(tag.size()).toString().trimmed();
        return true;
    };
    return match(QStringLiteral(":::csv"), QLatin1Char(','))
        || match(QStringLiteral(":::tsv"), QLatin1Char('\t'));
}

// A closing fence is a line that is exactly ":::" (≤3 leading spaces).
bool tableClose(const QString& line)
{
    int lead = 0;
    while (lead < line.size() && lead < 3 && line[lead] == QLatin1Char(' '))
        ++lead;
    return QStringView(line).mid(lead).trimmed() == QStringView(u":::");
}

// Phase 4 segmentation — pull :::csv/:::tsv blocks out (run after code blocks so
// a fence inside a code block is already protected). Unterminated → left as text.
QString extractTables(const QString& md, QVector<TableSeg>& segs)
{
    const QStringList lines = md.split(QLatin1Char('\n'));
    const int n = lines.size();
    QStringList out;
    int i = 0;
    while (i < n) {
        QChar delim;
        QString info;
        if (tableOpen(lines[i], &delim, &info)) {
            int close = -1;
            for (int j = i + 1; j < n; ++j)
                if (tableClose(lines[j])) { close = j; break; }
            if (close >= 0) {
                TableSeg t;
                t.delim = delim;
                t.info = info;
                QStringList body;
                for (int b = i + 1; b < close; ++b)
                    body << lines[b];
                t.body = body.join(QLatin1Char('\n'));
                out << QString() << placeholderTable(segs.size()) << QString();
                segs.push_back(t);
                i = close + 1;
                continue;
            }
            // Unterminated: fall through, treat the open line as ordinary text.
        }
        out << lines[i];
        ++i;
    }
    return out.join(QLatin1Char('\n'));
}

// Substitute each table placeholder with its rendered <table> (positionally, so
// generated HTML is never reinterpreted as regex backrefs).
QString spliceTables(QString html, const QVector<TableSeg>& segs, const Theme& th)
{
    for (int k = 0; k < segs.size(); ++k) {
        const QRegularExpression re(
            QStringLiteral("<p[^>]*>%1</p>").arg(placeholderTable(k)));
        const QRegularExpressionMatch m = re.match(html);
        if (!m.hasMatch())
            continue;
        const QString tbl =
            renderCsvTable(segs[k].info, segs[k].delim, segs[k].body, th);
        html.replace(m.capturedStart(), m.capturedLength(), tbl);
    }
    return html;
}

// Phase 3 — blockquote styling. Qt renders a blockquote as a plain <p> with a
// symmetric 40px margin and no <blockquote> element, and its rich-text engine
// ignores CSS borders on paragraphs. So instead of a real left border we give
// quote paragraphs a background tint plus a leading bar glyph (▎) — both of
// which Qt does render. Matched on the distinctive margin signature.
QString styleBlockquotes(QString html, const Theme& th)
{
    QRegularExpression bq(QStringLiteral(
        "<p style=\"([^\"]*margin-left:40px; margin-right:40px;[^\"]*)\">(.*?)</p>"));
    bq.setPatternOptions(QRegularExpression::DotMatchesEverythingOption);
    const QString repl = QStringLiteral(
        "<p style=\"\\1 background-color:%1;\">"
        "<span style=\"color:%2;\">▎ </span>\\2</p>")
        .arg(th.quoteBg.name(), th.subtleText.name());
    return html.replace(bq, repl);
}

// Phase 6 — `[[wikilinks]]`. `[[Title]]` survives Qt's markdown as literal text
// (no matching reference definition), so we linkify it here. A resolvable title
// becomes an in-app anchor (zb://note/ID, handled by the preview); an unknown
// title is left as plain `[[Title]]`. Built incrementally (not regex-replace)
// so the generated anchors aren't reinterpreted as capture backrefs.
QString linkifyWikilinks(const QString& html, const RenderContext& ctx, const Theme& th)
{
    if (!ctx.store)
        return html;
    static const QRegularExpression re(QStringLiteral("\\[\\[([^\\[\\]]+)\\]\\]"));
    QString out;
    int last = 0;
    auto it = re.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += html.mid(last, m.capturedStart() - last);
        last = m.capturedEnd();

        const QString display = m.captured(1); // as it appears (may be HTML-escaped)
        QString title = display;
        title.replace(QStringLiteral("&amp;"), QStringLiteral("&"))
            .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
            .replace(QStringLiteral("&gt;"), QStringLiteral(">"));
        const Id id = ctx.store->noteIdByTitle(title.trimmed());
        if (id > 0)
            out += QStringLiteral("<a href=\"zb://note/%1\" style=\"color:%2;\">%3</a>")
                       .arg(QString::number(id), th.accent.name(), display);
        else
            out += m.captured(0); // unresolved: leave as literal [[Title]]
    }
    out += html.mid(last);
    return out;
}

// The post-process stage: operates on rendered HTML. Task checkboxes are left
// as Qt's native ☐/☒ task-list markers (Qt draws those from the list-item
// marker type and ignores CSS, so there's nothing to do here).
QString postProcess(QString html, const RenderContext& ctx, const Theme& th)
{
    html = styleBlockquotes(std::move(html), th);
    html = linkifyWikilinks(html, ctx, th);
    return html;
}

} // namespace

QString MarkdownRenderer::toHtml(const QString& markdown, const RenderContext& ctx)
{
    // Pipeline: segment → renderStandard → splice → postProcess. Code blocks are
    // the first custom segment (Phase 3); the theme drives syntax + quote colors
    // and is pulled live so the editor's theme switch re-renders correctly.
    const Theme& th = currentTheme();
    // Extract verbatim/custom blocks before the standard pass: code first (most
    // sacred), then tables; hard-wrap runs last on the remaining prose.
    QVector<CodeSeg> codeSegs;
    QVector<TableSeg> tableSegs;
    QString prepared = extractCodeBlocks(markdown, codeSegs);
    prepared = extractTables(prepared, tableSegs);
    prepared = applyHardBreaks(prepared);

    QString html = renderStandardMarkdown(prepared);
    html = spliceCodeBlocks(std::move(html), codeSegs, th);
    html = spliceTables(std::move(html), tableSegs, th);
    html = postProcess(std::move(html), ctx, th);
    return html;
}

} // namespace zb
