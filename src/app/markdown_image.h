#pragma once

// Image embeds for note markdown (Phase 5; see markdown_enrichment.md Deep Dive
// B). Images are content-addressed by sha256 and referenced in the body as
// `![alt](zb-img:ID)` (with an optional `?maxwidth=N`). Storage backend (SQLite
// blob vs. files beside the db) is a Settings choice; these helpers hide it.

#include <QString>

class QByteArray;
class QImage;

namespace zb {

class Store;

// Import raw image bytes into the store under the active backend, returning the
// markdown token to insert ("![image](zb-img:ID)"), or an empty string if the
// data isn't a decodable image. Deduped by content (sha256).
QString importImageData(Store& store, const QByteArray& data);

// Read an image file from disk and import it.
QString importImageFile(Store& store, const QString& path);

// Resolve a zb-img reference to a QImage, scaled to maxWidth when >0 and wider
// (aspect preserved). Returns a null QImage if the id is unknown/unreadable.
QImage loadImageResource(Store& store, qint64 id, int maxWidth);

// Mark-and-sweep: delete image rows referenced by no note body (and their disk
// files, if any). Returns the number of images reclaimed.
int sweepOrphanImages(Store& store);

// Move every stored image to the given backend (true = disk, false = blob),
// writing/removing files and repointing rows. No-op for already-correct rows.
void migrateImageStorage(Store& store, bool toDisk);

} // namespace zb
