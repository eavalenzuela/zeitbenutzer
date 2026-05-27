#include "app/markdown_renderer.h"

#include "app/code_highlight.h"
#include "app/theme.h"
#include "app/typography.h"

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

// The post-process stage: operates on rendered HTML. Task checkboxes are left
// as Qt's native ☐/☒ task-list markers (Qt draws those from the list-item
// marker type and ignores CSS, so there's nothing to do here). Later phases add
// image src rewriting and wikilink anchors.
QString postProcess(QString html, const RenderContext& /*ctx*/, const Theme& th)
{
    html = styleBlockquotes(std::move(html), th);
    return html;
}

} // namespace

QString MarkdownRenderer::toHtml(const QString& markdown, const RenderContext& ctx)
{
    // Pipeline: segment → renderStandard → splice → postProcess. Code blocks are
    // the first custom segment (Phase 3); the theme drives syntax + quote colors
    // and is pulled live so the editor's theme switch re-renders correctly.
    const Theme& th = currentTheme();
    QVector<CodeSeg> segs;
    const QString prepared = applyHardBreaks(extractCodeBlocks(markdown, segs));
    QString html = renderStandardMarkdown(prepared);
    html = spliceCodeBlocks(std::move(html), segs, th);
    html = postProcess(std::move(html), ctx, th);
    return html;
}

} // namespace zb
