#include "app/code_highlight.h"

#include "app/theme.h"

#include <QHash>
#include <QSet>

namespace zb {

namespace {

QString esc(const QString& s)
{
    QString o;
    o.reserve(s.size());
    for (const QChar c : s) {
        if (c == QLatin1Char('&'))      o += QStringLiteral("&amp;");
        else if (c == QLatin1Char('<')) o += QStringLiteral("&lt;");
        else if (c == QLatin1Char('>')) o += QStringLiteral("&gt;");
        else                            o += c;
    }
    return o;
}

QString span(const QColor& c, const QString& text)
{
    return QStringLiteral("<span style=\"color:%1;\">%2</span>")
        .arg(c.name(), esc(text));
}

// A language's lexical shape. Defaults suit a C-family-ish language; each spec
// flips on what it needs.
struct LangSpec {
    QSet<QString> keywords;
    QString lineComment;       // "" = none (e.g. "#", "//")
    bool    blockComment = false; // /* ... */
    bool    tripleStrings = false; // ''' / """ (Python)
    bool    templateStr = false;   // backtick strings (JS)
    bool    singleQuote = true;
    bool    doubleQuote = true;
};

QSet<QString> words(const char* s)
{
    const QStringList l =
        QString::fromLatin1(s).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return QSet<QString>(l.cbegin(), l.cend());
}

// Resolve a fence info-string (already lowercased) to a spec, via aliases.
// Returns nullptr for unknown languages.
const LangSpec* specFor(const QString& lang)
{
    static const QHash<QString, QString> alias = {
        {QStringLiteral("py"), QStringLiteral("python")},
        {QStringLiteral("python"), QStringLiteral("python")},
        {QStringLiteral("sh"), QStringLiteral("shell")},
        {QStringLiteral("bash"), QStringLiteral("shell")},
        {QStringLiteral("shell"), QStringLiteral("shell")},
        {QStringLiteral("zsh"), QStringLiteral("shell")},
        {QStringLiteral("js"), QStringLiteral("js")},
        {QStringLiteral("javascript"), QStringLiteral("js")},
        {QStringLiteral("jsx"), QStringLiteral("js")},
        {QStringLiteral("node"), QStringLiteral("js")},
        {QStringLiteral("ts"), QStringLiteral("js")},
        {QStringLiteral("typescript"), QStringLiteral("js")},
        {QStringLiteral("tsx"), QStringLiteral("js")},
        {QStringLiteral("json"), QStringLiteral("json")},
        {QStringLiteral("yaml"), QStringLiteral("yaml")},
        {QStringLiteral("yml"), QStringLiteral("yaml")},
    };

    static const QHash<QString, LangSpec> specs = [] {
        QHash<QString, LangSpec> m;

        LangSpec py;
        py.lineComment = QStringLiteral("#");
        py.tripleStrings = true;
        py.keywords = words(
            "def class lambda if elif else for while break continue return yield "
            "pass import from as with try except finally raise assert global "
            "nonlocal del in is not and or None True False async await match case "
            "print self");
        m.insert(QStringLiteral("python"), py);

        LangSpec sh;
        sh.lineComment = QStringLiteral("#");
        sh.keywords = words(
            "if then elif else fi for while until do done case esac function in "
            "select return export local readonly declare unset shift source eval "
            "exec set test true false echo");
        m.insert(QStringLiteral("shell"), sh);

        LangSpec js;
        js.lineComment = QStringLiteral("//");
        js.blockComment = true;
        js.templateStr = true;
        js.keywords = words(
            "var let const function return if else for while do switch case break "
            "continue new delete typeof instanceof void this super class extends "
            "implements interface type enum import export from default as async "
            "await try catch finally throw yield in of null undefined true false "
            "public private protected readonly static get set namespace declare "
            "abstract");
        m.insert(QStringLiteral("js"), js);

        LangSpec json;
        json.singleQuote = false; // JSON strings are double-quoted only
        json.keywords = words("true false null");
        m.insert(QStringLiteral("json"), json);

        LangSpec yaml;
        yaml.lineComment = QStringLiteral("#");
        yaml.keywords = words("true false null yes no on off");
        m.insert(QStringLiteral("yaml"), yaml);

        return m;
    }();

    const auto it = alias.constFind(lang);
    if (it == alias.cend())
        return nullptr;
    const auto sit = specs.constFind(it.value());
    return sit == specs.cend() ? nullptr : &sit.value();
}

} // namespace

QString highlightCode(const QString& code, const QString& lang, const Theme& th)
{
    // Qt's rich-text engine keeps spaces under white-space:pre but drops the
    // newlines between inline <span>s, so line breaks must be explicit <br/>.
    const auto withBreaks = [](QString html) {
        return html.replace(QLatin1Char('\n'), QStringLiteral("<br/>"));
    };

    const LangSpec* sp = specFor(lang.trimmed().toLower());
    if (!sp)
        return withBreaks(esc(code)); // unknown language: verbatim, uncolored

    const int n = code.size();
    QString out;
    QString plain; // run of default (uncolored) text, escaped on flush
    const auto flush = [&] {
        if (!plain.isEmpty()) {
            out += esc(plain);
            plain.clear();
        }
    };
    const auto isIdentStart = [](QChar c) { return c.isLetter() || c == QLatin1Char('_'); };
    const auto isIdent = [](QChar c) { return c.isLetterOrNumber() || c == QLatin1Char('_'); };
    const auto isDelim = [&](QChar q) {
        return (q == QLatin1Char('"') && sp->doubleQuote)
            || (q == QLatin1Char('\'') && sp->singleQuote)
            || (q == QLatin1Char('`') && sp->templateStr);
    };

    int i = 0;
    while (i < n) {
        const QChar c = code[i];

        // Line comment.
        if (!sp->lineComment.isEmpty()
            && QStringView(code).mid(i, sp->lineComment.size()) == sp->lineComment) {
            flush();
            int j = i;
            while (j < n && code[j] != QLatin1Char('\n'))
                ++j;
            out += span(th.synComment, code.mid(i, j - i));
            i = j;
            continue;
        }
        // Block comment /* ... */.
        if (sp->blockComment && c == QLatin1Char('/') && i + 1 < n
            && code[i + 1] == QLatin1Char('*')) {
            flush();
            int j = i + 2;
            while (j + 1 < n && !(code[j] == QLatin1Char('*') && code[j + 1] == QLatin1Char('/')))
                ++j;
            j = (j + 1 < n) ? j + 2 : n;
            out += span(th.synComment, code.mid(i, j - i));
            i = j;
            continue;
        }
        // String literal.
        if (isDelim(c)) {
            flush();
            const QChar q = c;
            if (sp->tripleStrings && i + 2 < n && code[i + 1] == q && code[i + 2] == q) {
                int j = i + 3;
                while (j + 2 < n
                       && !(code[j] == q && code[j + 1] == q && code[j + 2] == q))
                    ++j;
                j = (j + 2 < n) ? j + 3 : n;
                out += span(th.synString, code.mid(i, j - i));
                i = j;
                continue;
            }
            int j = i + 1;
            while (j < n) {
                if (code[j] == QLatin1Char('\\')) { j += 2; continue; }
                if (code[j] == q) { ++j; break; }
                if (code[j] == QLatin1Char('\n') && q != QLatin1Char('`')) break; // unterminated
                ++j;
            }
            j = qMin(j, n);
            out += span(th.synString, code.mid(i, j - i));
            i = j;
            continue;
        }
        // Number.
        if (c.isDigit()) {
            flush();
            int j = i;
            while (j < n
                   && (code[j].isLetterOrNumber() || code[j] == QLatin1Char('.')
                       || code[j] == QLatin1Char('_')))
                ++j;
            out += span(th.synNumber, code.mid(i, j - i));
            i = j;
            continue;
        }
        // Identifier / keyword.
        if (isIdentStart(c)) {
            int j = i;
            while (j < n && isIdent(code[j]))
                ++j;
            const QString word = code.mid(i, j - i);
            if (sp->keywords.contains(word)) {
                flush();
                out += span(th.synKeyword, word);
            } else {
                plain += word;
            }
            i = j;
            continue;
        }

        plain += c;
        ++i;
    }
    flush();
    return withBreaks(out);
}

} // namespace zb
