#pragma once

// Module 6: fetches configured external calendars and refreshes their cached
// events in the store. File sources are read synchronously; URL sources are
// fetched over HTTPS (webcal:// is rewritten to https://) — covering Google /
// iCloud / Outlook via their "secret iCal address", no OAuth.
//
// Parsing/expansion live in storage (headless); this class is the I/O edge.
// Events are cached over a broad window so week-to-week navigation reads from
// the cache without re-fetching. Refresh is manual + on-launch.

#include <QByteArray>
#include <QObject>
#include <QNetworkAccessManager>

#include "storage/types.h"

namespace zb {

class Store;

class ExternalSync : public QObject {
    Q_OBJECT
public:
    explicit ExternalSync(Store& store, QObject* parent = nullptr);

    void refreshAll();                       // every enabled source
    void refreshSource(const ExternalSource& src);

signals:
    void refreshed();                        // a source's cache changed
    void failed(const QString& message);     // a fetch/parse problem (non-fatal)

private:
    // Parse + expand a payload and replace the source's cached events.
    void ingest(const ExternalSource& src, const QByteArray& data);

    Store&                m_store;
    QNetworkAccessManager m_net;
};

} // namespace zb
