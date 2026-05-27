#include "app/note_editor.h"

#include "app/markdown_image.h"
#include "app/markdown_renderer.h"
#include "app/theme.h"
#include "app/typography.h"
#include "storage/store.h"

#include <QBuffer>
#include <QFileDialog>
#include <QHash>
#include <QHBoxLayout>
#include <QImage>
#include <QLineEdit>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTextBrowser>
#include <QTimer>
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

// Body editor that accepts pasted/dropped images: imports them and inserts the
// `![image](zb-img:ID)` token in place.
class MarkdownBodyEdit : public QPlainTextEdit {
public:
    MarkdownBodyEdit(Store& store, QWidget* parent) : QPlainTextEdit(parent), m_store(store) {}

protected:
    bool canInsertFromMimeData(const QMimeData* src) const override
    {
        return src->hasImage() || src->hasUrls()
            || QPlainTextEdit::canInsertFromMimeData(src);
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
    Store& m_store;
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
    layout->addLayout(header);

    auto* split = new QSplitter(Qt::Horizontal, this);
    m_body = new MarkdownBodyEdit(store, split);
    m_body->setPlaceholderText(QStringLiteral("Write in Markdown…"));
    m_body->setFont(editorFont()); // monospace source of truth
    m_preview = new MarkdownPreview(store, split);
    m_preview->setOpenExternalLinks(true);
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
        m_preview->setHtml(MarkdownRenderer::toHtml(n.bodyMd, {m_noteId}));
        setEditingEnabled(true);
    }
    m_loading = false;
}

void NoteEditor::onEdited()
{
    if (m_loading || m_noteId <= 0)
        return;
    m_preview->setHtml(MarkdownRenderer::toHtml(m_body->toPlainText(), {m_noteId}));
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
        m_preview->setHtml(MarkdownRenderer::toHtml(m_body->toPlainText(),
                                                    {m_noteId})); // re-render w/ new CSS
}

} // namespace zb
