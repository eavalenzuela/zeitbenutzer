#include "app/markdown_renderer.h"

#include <QTextDocument>

#include <utility>

namespace zb {

namespace {

// Standard markdown → HTML. Uses Qt's own parser (md4c under the hood) and
// serializer, with the same GitHub dialect QTextBrowser::setMarkdown defaults
// to — so Phase 1 reproduces the prior preview output. If this serialization
// proves too lossy once we splice custom HTML into it, swap this one function
// for a vendored md4c emitting HTML directly (see markdown_enrichment.md).
QString renderStandardMarkdown(const QString& md)
{
    QTextDocument doc;
    doc.setMarkdown(md, QTextDocument::MarkdownDialectGitHub);
    return doc.toHtml();
}

// Phase 2 — task checkboxes. Qt's GitHub dialect already turns `- [ ]`/`- [x]`
// into task-list items, emitting the marker glyphs ☐ (U+2610) and ☒ (U+2612)
// in the document's <style> block. We prefer a check mark ☑ (U+2611) for done
// items, so swap that one glyph. If Qt's serialization ever changes, the
// replace simply no-ops and we fall back to Qt's glyph rather than breaking.
QString applyCheckboxGlyph(QString html)
{
    return html.replace(QStringLiteral("content: \"\\2612\";"),
                        QStringLiteral("content: \"\\2611\";"));
}

// The post-process stage: operates on rendered HTML. Later phases add image src
// rewriting and wikilink anchors here.
QString postProcess(QString html, const RenderContext& /*ctx*/)
{
    html = applyCheckboxGlyph(std::move(html));
    return html;
}

} // namespace

QString MarkdownRenderer::toHtml(const QString& markdown, const RenderContext& ctx)
{
    // Phase 1 established the seam; later phases slot stages in around this:
    //   segment → renderStandard → splice → postProcess
    // Custom block segmentation (Phase 4+) is not wired yet, so the whole body
    // is standard markdown today.
    QString html = renderStandardMarkdown(markdown);
    html = postProcess(std::move(html), ctx);
    return html;
}

} // namespace zb
