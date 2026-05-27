#include "app/markdown_table.h"

#include "app/theme.h"

#include <QStringList>
#include <QTextDocument>
#include <QVector>

namespace zb {

namespace {

// Strip surrounding whitespace, then surrounding quotes (un-doubling "" → ").
// Unquoted cells are trimmed; quoted cells preserve their interior spaces.
QString dequote(QString cell)
{
    cell = cell.trimmed();
    if (cell.size() >= 2 && cell.startsWith(QLatin1Char('"'))
        && cell.endsWith(QLatin1Char('"'))) {
        cell = cell.mid(1, cell.size() - 2);
        cell.replace(QStringLiteral("\"\""), QStringLiteral("\""));
    }
    return cell;
}

// Split one line into dequoted cells. Delimiters inside "quoted" fields don't
// separate; "" is an escaped quote. No multi-line fields (line-based by design).
QStringList parseCells(const QString& line, QChar delim)
{
    QStringList raw;
    QString cur;
    bool inQuotes = false;
    for (const QChar c : line) {
        if (c == QLatin1Char('"')) {
            inQuotes = !inQuotes;
            cur += c;
        } else if (c == delim && !inQuotes) {
            raw << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    raw << cur;
    for (QString& cell : raw)
        cell = dequote(cell);
    return raw;
}

// Inline markdown for a cell (bold/italic/code/links): render via Qt and pull
// the inner HTML out of the single wrapping <p>. (Shared concern with Phase 6
// wikilinks; a hand-rolled inline renderer can replace this later.)
QString cellHtml(const QString& text)
{
    if (text.isEmpty())
        return QString();
    QTextDocument doc;
    doc.setMarkdown(text, QTextDocument::MarkdownDialectGitHub);
    const QString html = doc.toHtml();
    int s = html.indexOf(QStringLiteral("<p"));
    if (s < 0)
        return text.toHtmlEscaped();
    s = html.indexOf(QLatin1Char('>'), s) + 1;
    const int e = html.indexOf(QStringLiteral("</p>"), s);
    return (e < 0 ? html.mid(s) : html.mid(s, e - s)).trimmed();
}

bool isNumeric(const QString& s)
{
    if (s.isEmpty())
        return false;
    bool ok = false;
    s.toDouble(&ok);
    return ok;
}

// Parse the fence info-line: leading `noheader` / key=value tokens, then the
// rest is the caption (original spacing preserved). See Deep Dive A.
struct TableOpts {
    QChar   delim;
    bool    header = true;
    QString caption;
};

TableOpts parseInfo(const QString& info, QChar defaultDelim)
{
    TableOpts o;
    o.delim = defaultDelim;
    QString rest = info.trimmed();
    while (!rest.isEmpty()) {
        const int sp = rest.indexOf(QLatin1Char(' '));
        const QString tok = (sp < 0) ? rest : rest.left(sp);
        if (tok == QStringLiteral("noheader")) {
            o.header = false;
        } else if (tok.contains(QLatin1Char('='))) {
            const QString key = tok.section(QLatin1Char('='), 0, 0);
            const QString val = tok.section(QLatin1Char('='), 1);
            if (key == QStringLiteral("delim") && !val.isEmpty())
                o.delim = val.at(0);
            // unknown key=value: consumed and ignored
        } else {
            o.caption = rest; // first non-option token begins the caption
            break;
        }
        if (sp < 0)
            break;
        rest = rest.mid(sp + 1).trimmed();
    }
    return o;
}

} // namespace

QString renderCsvTable(const QString& info, QChar defaultDelim,
                       const QString& body, const Theme& th)
{
    const TableOpts opt = parseInfo(info, defaultDelim);

    QVector<QStringList> grid;
    const QStringList lines = body.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.trimmed().isEmpty())
            continue; // skip blank rows
        grid.push_back(parseCells(line, opt.delim));
    }
    if (grid.isEmpty())
        return QString(); // empty block renders nothing

    int cols = 0;
    for (const QStringList& r : grid)
        cols = qMax(cols, r.size());
    for (QStringList& r : grid)
        while (r.size() < cols)
            r << QString(); // pad ragged rows

    // Per-column alignment: right-align a column when every non-empty data cell
    // parses as a number (and at least one is non-empty).
    const int firstData = opt.header ? 1 : 0;
    QVector<bool> rightAlign(cols, false);
    for (int c = 0; c < cols; ++c) {
        bool any = false, allNum = true;
        for (int r = firstData; r < grid.size(); ++r) {
            const QString& cell = grid[r][c];
            if (cell.isEmpty())
                continue;
            any = true;
            if (!isNumeric(cell)) {
                allNum = false;
                break;
            }
        }
        rightAlign[c] = any && allNum;
    }
    const auto align = [&](int c) {
        return rightAlign[c] ? QStringLiteral("right") : QStringLiteral("left");
    };

    QString out = QStringLiteral(
        "<table border=\"1\" cellspacing=\"0\" cellpadding=\"6\" "
        "style=\"border-color:%1;\">").arg(th.border.name());
    if (!opt.caption.isEmpty())
        out += QStringLiteral("<caption><b>%1</b></caption>").arg(cellHtml(opt.caption));

    int r = 0;
    if (opt.header) {
        out += QStringLiteral("<thead><tr>");
        for (int c = 0; c < cols; ++c)
            out += QStringLiteral("<th style=\"background-color:%1; text-align:%2;\">%3</th>")
                       .arg(th.codeBg.name(), align(c), cellHtml(grid[0][c]));
        out += QStringLiteral("</tr></thead>");
        r = 1;
    }
    out += QStringLiteral("<tbody>");
    for (; r < grid.size(); ++r) {
        out += QStringLiteral("<tr>");
        for (int c = 0; c < cols; ++c)
            out += QStringLiteral("<td style=\"text-align:%1;\">%2</td>")
                       .arg(align(c), cellHtml(grid[r][c]));
        out += QStringLiteral("</tr>");
    }
    out += QStringLiteral("</tbody></table>");
    return out;
}

} // namespace zb
