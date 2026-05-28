#include "app/syntax_help.h"

#include "app/typography.h"

#include <QDialog>
#include <QPointer>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace zb {

namespace {

// The cheat-sheet. Hand-written HTML (not run through the renderer) so syntax is
// shown literally beside what it does. Keep in sync with the renderer's actual
// support as it evolves. Inherits the app palette, so it tracks the theme.
QString helpHtml()
{
    const QString css =
        QStringLiteral(
            "code, pre { font-family: '%1'; } "
            "th { text-align: left; } "
            "td { padding-right: 14px; vertical-align: top; }")
            .arg(editorFont().family());

    const QString body = QStringLiteral(R"HTML(
<h2>Markdown syntax</h2>
<p>Single line breaks are kept (a blank line starts a new paragraph).</p>

<h3>Text</h3>
<table>
<tr><td><code>**bold**</code></td><td>bold</td></tr>
<tr><td><code>*italic*</code></td><td>italic</td></tr>
<tr><td><code>`code`</code></td><td>inline code</td></tr>
<tr><td><code># H1</code> … <code>###### H6</code></td><td>headings</td></tr>
<tr><td><code>&gt; quoted</code></td><td>block quote (tinted, with a bar)</td></tr>
<tr><td><code>---</code></td><td>horizontal rule</td></tr>
</table>

<h3>Lists &amp; tasks</h3>
<table>
<tr><td><code>- item</code> &nbsp; <code>1. item</code></td><td>bullet / numbered</td></tr>
<tr><td><code>- [ ] todo</code></td><td>unchecked box</td></tr>
<tr><td><code>- [x] done</code></td><td>checked box (toggle by editing the text)</td></tr>
</table>

<h3>Links &amp; images</h3>
<table>
<tr><td><code>[text](https://…)</code></td><td>external link (opens in browser)</td></tr>
<tr><td><code>[[Note title]]</code></td><td>link to another note — type <code>[[</code> for completion</td></tr>
<tr><td><code>![alt](…)</code></td><td>image — usually inserted for you</td></tr>
</table>
<p>Add an image by <b>pasting</b>, <b>dragging a file</b> onto the editor, or the
<b>Insert image…</b> button. Scale one with
<code>![image](zb-img:ID?maxwidth=400)</code>.</p>

<h3>Code blocks</h3>
<pre>```python
def greet(name):
    return f"hello {name}"
```</pre>
<p>Highlighted languages: <code>python</code>, <code>shell</code>/<code>bash</code>,
<code>js</code>/<code>ts</code>/<code>node</code>, <code>json</code>,
<code>yaml</code>. Others render plain.</p>

<h3>Tables</h3>
<pre>:::csv My caption
Task,Owner,Estimate
"Design, UX",Ed,3
Build,Ed,12
:::</pre>
<p>First row is the header; all-number columns right-align; quote a cell to keep
the delimiter inside it. Options after the tag:</p>
<table>
<tr><td><code>:::tsv</code></td><td>tab-separated</td></tr>
<tr><td><code>delim=;</code></td><td>use a different delimiter</td></tr>
<tr><td><code>noheader</code></td><td>no header row</td></tr>
<tr><td>trailing text</td><td>becomes the caption</td></tr>
</table>
<p>Close the block with a line containing only <code>:::</code>.</p>
)HTML");

    return QStringLiteral("<style>%1</style>%2").arg(css, body);
}

} // namespace

void showSyntaxHelp(QWidget* parent)
{
    static QPointer<QDialog> dlg;
    if (dlg) {
        dlg->show();
        dlg->raise();
        dlg->activateWindow();
        return;
    }

    auto* d = new QDialog(parent);
    d->setAttribute(Qt::WA_DeleteOnClose); // QPointer auto-nulls on delete
    d->setWindowTitle(QStringLiteral("Markdown syntax"));
    d->resize(560, 640);
    auto* layout = new QVBoxLayout(d);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* view = new QTextBrowser(d);
    view->setOpenExternalLinks(true);
    view->setHtml(helpHtml());
    layout->addWidget(view);

    dlg = d;
    d->show();
}

} // namespace zb
