#include "app/markdown_image.h"

#include "app/settings.h"
#include "storage/store.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QRegularExpression>
#include <QSet>

namespace zb {

namespace {

bool diskBackend()
{
    return Settings::instance().imageStorageBackend() == QStringLiteral("disk");
}

QString imagesDir()
{
    return Settings::instance().dataLocation() + QStringLiteral("/images");
}

QString sha256Hex(const QByteArray& data)
{
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

// Decodable-image format (e.g. "png", "jpeg") or empty if not an image.
QString detectFormat(const QByteArray& data)
{
    QBuffer buf;
    buf.setData(data);
    buf.open(QIODevice::ReadOnly);
    QImageReader reader(&buf);
    return QString::fromLatin1(reader.format()).toLower();
}

// Read an image row's raw bytes regardless of backend (blob inline, or the file
// at its relative path beside the db).
QByteArray imageBytes(const Image& img)
{
    if (!img.bytes.isEmpty())
        return img.bytes;
    if (img.path.isEmpty())
        return {};
    QFile f(Settings::instance().dataLocation() + QLatin1Char('/') + img.path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString relPathFor(const QString& sha, const QString& fmt)
{
    return QStringLiteral("images/%1.%2").arg(sha, fmt.isEmpty() ? QStringLiteral("img") : fmt);
}

} // namespace

QString importImageData(Store& store, const QByteArray& data)
{
    const QString fmt = detectFormat(data);
    if (fmt.isEmpty())
        return {}; // not a decodable image
    const QString sha = sha256Hex(data);
    const QString mime = QStringLiteral("image/%1").arg(fmt);

    Id id = -1;
    if (diskBackend()) {
        const QString rel = relPathFor(sha, fmt);
        QDir().mkpath(imagesDir());
        const QString abs = Settings::instance().dataLocation() + QLatin1Char('/') + rel;
        if (!QFile::exists(abs)) {
            QFile f(abs);
            if (f.open(QIODevice::WriteOnly))
                f.write(data);
        }
        id = store.putImage(sha, mime, QByteArray(), rel);
    } else {
        id = store.putImage(sha, mime, data, QString());
    }
    return id > 0 ? QStringLiteral("![image](zb-img:%1)").arg(id) : QString();
}

QString importImageFile(Store& store, const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return importImageData(store, f.readAll());
}

QImage loadImageResource(Store& store, qint64 id, int maxWidth)
{
    const Image img = store.image(id);
    if (img.id <= 0)
        return {};
    QImage out;
    out.loadFromData(imageBytes(img));
    if (!out.isNull() && maxWidth > 0 && out.width() > maxWidth)
        out = out.scaledToWidth(maxWidth, Qt::SmoothTransformation);
    return out;
}

int sweepOrphanImages(Store& store)
{
    // Collect every image id referenced from a note body.
    static const QRegularExpression ref(QStringLiteral("zb-img:(\\d+)"));
    QSet<Id> referenced;
    for (const QString& body : store.allNoteBodies()) {
        auto it = ref.globalMatch(body);
        while (it.hasNext())
            referenced.insert(it.next().captured(1).toLongLong());
    }

    int reclaimed = 0;
    const QString root = Settings::instance().dataLocation() + QLatin1Char('/');
    for (const Id id : store.imageIds()) {
        if (referenced.contains(id))
            continue;
        const Image img = store.image(id);
        if (!img.path.isEmpty())
            QFile::remove(root + img.path);
        store.deleteImage(id);
        ++reclaimed;
    }
    return reclaimed;
}

void migrateImageStorage(Store& store, bool toDisk)
{
    const QString root = Settings::instance().dataLocation() + QLatin1Char('/');
    for (const Id id : store.imageIds()) {
        const Image img = store.image(id);
        if (toDisk && img.path.isEmpty()) {
            // blob → disk
            const QString fmt = img.mime.section(QLatin1Char('/'), 1);
            const QString rel = relPathFor(img.sha256, fmt);
            QDir().mkpath(imagesDir());
            QFile f(root + rel);
            if (f.open(QIODevice::WriteOnly)) {
                f.write(img.bytes);
                f.close();
                store.setImageStorage(id, QByteArray(), rel);
            }
        } else if (!toDisk && !img.path.isEmpty()) {
            // disk → blob
            const QByteArray bytes = imageBytes(img);
            if (!bytes.isEmpty()) {
                store.setImageStorage(id, bytes, QString());
                QFile::remove(root + img.path);
            }
        }
    }
}

} // namespace zb
