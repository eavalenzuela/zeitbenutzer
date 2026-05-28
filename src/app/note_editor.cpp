#include "app/note_editor.h"

#include "app/markdown_image.h"
#include "app/markdown_renderer.h"
#include "app/syntax_help.h"
#include "app/theme.h"
#include "app/typography.h"
#include "storage/store.h"

#include <QAbstractItemView>
#include <QBuffer>
#include <QCompleter>
#include <QDesktopServices>
#include <QFileDialog>
#include <QHash>
#include <QHBoxLayout>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStringListModel>
#include <QStyle>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

namespace zb {

namespace {

// Preview pane that resolves `zb-img:ID?maxwidth=N` image URLs from the store,
// caching decoded (and scaled) images so the per-keystroke re-render stays
// smooth. No Q_OBJECT: only a virtual override, no new signals/slots.
class MarkdownPreview : public QTextBrowser {
public:
    MarkdownPreview(Store& store, QWidget* parent) : QTextBrowser(parent), m_store(store) {}

    QVariant loadResource(int type, const QUrl& name) override
    {
        if (name.scheme() == QLatin1String("zb-img")) {
            const qint64 id = name.path().toLongLong();
            int maxw = 0;
            const QUrlQuery q(name);
            if (q.hasQueryItem(QStringLiteral("maxwidth")))
                maxw = q.queryItemValue(QStringLiteral("maxwidth")).toInt();
            const QString key = QStringLiteral("%1?%2").arg(id).arg(maxw);
            auto it = m_cache.constFind(key);
            if (it == m_cache.constEnd())
                it = m_cache.insert(key, loadImageResource(m_store, id, maxw));
            return it.value();
        }
        return QTextBrowser::loadResource(type, name);
    }

private:
    Store& m_store;
    QHash<QString, QImage> m_cache;
};

// Body editor that (a) accepts pasted/dropped images — importing them and
// inserting the `![image](zb-img:ID)` token — and (b) offers `[[`-triggered
// note-title completion for wikilinks.
class MarkdownBodyEdit : public QPlainTextEdit {
public:
    MarkdownBodyEdit(Store& store, QWidget* parent) : QPlainTextEdit(parent), m_store(store)
    {
        m_completer = new QCompleter(this);
        m_completer->setWidget(this);
        m_completer->setCompletionMode(QCompleter::PopupCompletion);
        m_completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_model = new QStringListModel(m_completer);
        m_completer->setModel(m_model);
        connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated),
                this, [this](const QString& title) { insertCompletion(title); });
    }

protected:
    bool canInsertFromMimeData(const QMimeData* src) const override
    {
        return src->hasImage() || src->hasUrls()
            || QPlainTextEdit::canInsertFromMimeData(src);
    }

    void keyPressEvent(QKeyEvent* e) override
    {
        // While the popup is open, let it consume navigation/accept keys.
        if (m_completer->popup()->isVisible()) {
            switch (e->key()) {
            case Qt::Key_Return:
            case Qt::Key_Enter:
            case Qt::Key_Escape:
            case Qt::Key_Tab:
            case Qt::Key_Backtab:
                e->ignore();
                return;
            default:
                break;
            }
        }
        QPlainTextEdit::keyPressEvent(e);
        updateCompleter();
    }

    void insertFromMimeData(const QMimeData* src) override
    {
        if (src->hasImage()) {
            const QImage img = qvariant_cast<QImage>(src->imageData());
            QByteArray data;
            QBuffer buf(&data);
            buf.open(QIODevice::WriteOnly);
            img.save(&buf, "PNG");
            const QString token = importImageData(m_store, data);
            if (!token.isEmpty()) {
                textCursor().insertText(token);
                return;
            }
        }
        if (src->hasUrls()) {
            QString tokens;
            for (const QUrl& u : src->urls()) {
                if (!u.isLocalFile())
                    continue;
                const QString t = importImageFile(m_store, u.toLocalFile());
                if (!t.isEmpty())
                    tokens += t + QLatin1Char('\n');
            }
            if (!tokens.isEmpty()) {
                textCursor().insertText(tokens);
                return;
            }
        }
        QPlainTextEdit::insertFromMimeData(src);
    }

private:
    // The text typed after an unclosed `[[` on the current line, or a null
    // QString when the cursor isn't inside a wikilink.
    QString wikilinkPrefix() const
    {
        const QTextCursor c = textCursor();
        const QString before = c.block().text().left(c.positionInBlock());
        const int open = before.lastIndexOf(QStringLiteral("[["));
        if (open < 0 || before.indexOf(QStringLiteral("]]"), open) >= 0)
            return QString(); // not inside an open wikilink
        return before.mid(open + 2);
    }

    void updateCompleter()
    {
        const QString prefix = wikilinkPrefix();
        if (prefix.isNull()) {
            m_completer->popup()->hide();
            return;
        }
        m_model->setStringList(m_store.noteTitles()); // cheap; refresh each time
        m_completer->setCompletionPrefix(prefix);
        if (m_completer->completionCount() == 0) {
            m_completer->popup()->hide();
            return;
        }
        m_completer->popup()->setCurrentIndex(
            m_completer->completionModel()->index(0, 0));
        QRect r = cursorRect();
        r.setWidth(m_completer->popup()->sizeHintForColumn(0) + 32);
        m_completer->complete(r);
    }

    void insertCompletion(const QString& title)
    {
        const QString prefix = wikilinkPrefix();
        if (prefix.isNull())
            return;
        QTextCursor c = textCursor();
        for (int i = 0; i < prefix.size(); ++i)
            c.deletePreviousChar();
        c.insertText(title + QStringLiteral("]]"));
        setTextCursor(c);
        m_completer->popup()->hide();
    }

    Store& m_store;
    QCompleter* m_completer;
    QStringListModel* m_model;
};

} // namespace

NoteEditor::NoteEditor(Store& store, QWidget* parent)
    : QWidget(parent), m_store(store)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    // Title row: the title field plus an image-insert button.
    auto* header = new QHBoxLayout;
    m_title = new QLineEdit(this);
    m_title->setPlaceholderText(QStringLiteral("Title"));
    header->addWidget(m_title, 1);
    m_insertImage = new QPushButton(QStringLiteral("Insert image…"), this);
    connect(m_insertImage, &QPushButton::clicked, this, &NoteEditor::insertImage);
    header->addWidget(m_insertImage);
    auto* help = new QToolButton(this);
    help->setAutoRaise(true);
    help->setIcon(style()->standardIcon(QStyle::SP_MessageBoxInformation));
    help->setToolTip(QStringLiteral("Markdown syntax"));
    connect(help, &QToolButton::clicked, this, [this] { showSyntaxHelp(this); });
    header->addWidget(help);
    layout->addLayout(header);

    auto* split = new QSplitter(Qt::Horizontal, this);
    m_body = new MarkdownBodyEdit(store, split);
    m_body->setPlaceholderText(QStringLiteral("Write in Markdown…"));
    m_body->setFont(editorFont()); // monospace source of truth
    m_preview = new MarkdownPreview(store, split);
    m_preview->setOpenLinks(false); // route clicks ourselves: zb:// in-app, else browser
    connect(m_preview, &QTextBrowser::anchorClicked, this, &NoteEditor::onAnchorClicked);
    m_preview->setFont(uiFont());  // proportional rendered prose
    stylePreview(m_preview, currentTheme()); // code spans → mono, themed links
    split->addWidget(m_body);
    split->addWidget(m_preview);
    split->setSizes({1, 1});
    layout->addWidget(split, 1);

    // Debounced autosave: coalesce keystrokes, write ~700ms after the last one.
    m_saveTimer = new QTimer(this);
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(700);
    connect(m_saveTimer, &QTimer::timeout, this, &NoteEditor::save);

    connect(m_title, &QLineEdit::textChanged, this, &NoteEditor::onEdited);
    connect(m_body, &QPlainTextEdit::textChanged, this, &NoteEditor::onEdited);

    setEditingEnabled(false);
}

void NoteEditor::setEditingEnabled(bool on)
{
    m_title->setEnabled(on);
    m_body->setEnabled(on);
    m_insertImage->setEnabled(on);
}

void NoteEditor::insertImage()
{
    if (m_noteId <= 0)
        return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Insert image"), QString(),
        QStringLiteral("Images (*.png *.jpg *.jpeg *.gif *.webp *.bmp)"));
    if (path.isEmpty())
        return;
    const QString token = importImageFile(m_store, path);
    if (!token.isEmpty())
        m_body->textCursor().insertText(token); // triggers onEdited → save + render
}

void NoteEditor::onAnchorClicked(const QUrl& url)
{
    // In-app wikilinks (zb://note/ID) open the target note; everything else is
    // an external link → hand off to the system browser.
    if (url.scheme() == QLatin1String("zb") && url.host() == QLatin1String("note")) {
        const Id id = url.path().mid(1).toLongLong(); // "/123" → 123
        if (id > 0)
            emit noteLinkActivated(id);
        return;
    }
    QDesktopServices::openUrl(url);
}

void NoteEditor::loadNote(Id noteId)
{
    // Persist whatever was being edited before switching away.
    flush();

    m_noteId = noteId;
    m_loading = true;

    if (noteId <= 0) {
        m_title->clear();
        m_body->clear();
        m_preview->clear();
        setEditingEnabled(false);
    } else {
        const Note n = m_store.note(noteId);
        m_title->setText(n.title);
        m_body->setPlainText(n.bodyMd);
        m_preview->setHtml(MarkdownRenderer::toHtml(n.bodyMd, {m_noteId, &m_store}));
        setEditingEnabled(true);
    }
    m_loading = false;
}

void NoteEditor::onEdited()
{
    if (m_loading || m_noteId <= 0)
        return;
    m_preview->setHtml(
        MarkdownRenderer::toHtml(m_body->toPlainText(), {m_noteId, &m_store}));
    m_saveTimer->start(); // (re)arm debounce
}

void NoteEditor::save()
{
    if (m_noteId <= 0)
        return;
    const QString title = m_title->text();
    m_store.updateNote(m_noteId, title, m_body->toPlainText());
    emit noteTitleChanged(m_noteId, title);
}

void NoteEditor::flush()
{
    if (m_saveTimer->isActive()) {
        m_saveTimer->stop();
        save();
    }
}

void NoteEditor::refreshTheme()
{
    stylePreview(m_preview, currentTheme());
    if (m_noteId > 0)
        m_preview->setHtml(MarkdownRenderer::toHtml(
            m_body->toPlainText(), {m_noteId, &m_store})); // re-render w/ new CSS
}

} // namespace zb
