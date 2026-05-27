#include "app/external_sync.h"

#include "storage/ical.h"
#include "storage/store.h"

#include <QFile>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace zb {

ExternalSync::ExternalSync(Store& store, QObject* parent)
    : QObject(parent), m_store(store)
{
}

void ExternalSync::refreshAll()
{
    for (const ExternalSource& src : m_store.listExternalSources())
        if (src.enabled)
            refreshSource(src);
}

void ExternalSync::refreshSource(const ExternalSource& src)
{
    if (src.kind == ExternalSource::Kind::File) {
        QFile f(src.location);
        if (!f.open(QIODevice::ReadOnly)) {
            emit failed(QStringLiteral("Cannot read %1: %2")
                            .arg(src.name, f.errorString()));
            return;
        }
        ingest(src, f.readAll());
        return;
    }

    // URL: normalise webcal:// to https:// and fetch asynchronously.
    QString loc = src.location;
    if (loc.startsWith(QStringLiteral("webcal://"), Qt::CaseInsensitive))
        loc = QStringLiteral("https://") + loc.mid(9);

    QNetworkRequest req{QUrl(loc)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_net.get(req);
    const ExternalSource captured = src;
    connect(reply, &QNetworkReply::finished, this, [this, reply, captured] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(QStringLiteral("Fetch failed for %1: %2")
                            .arg(captured.name, reply->errorString()));
            return;
        }
        ingest(captured, reply->readAll());
    });
}

void ExternalSync::ingest(const ExternalSource& src, const QByteArray& data)
{
    QString err;
    const QList<ICalEvent> parsed = parseICalendar(data, &err);
    if (parsed.isEmpty() && !err.isEmpty()) {
        emit failed(QStringLiteral("%1: %2").arg(src.name, err));
        return;
    }

    // Cache a broad window so navigating weeks doesn't require a re-fetch.
    const QDateTime now = QDateTime::currentDateTime();
    const QList<ICalEvent> instances =
        expandICalEvents(parsed, now.addDays(-60), now.addDays(120));

    QList<ExternalEvent> events;
    events.reserve(instances.size());
    for (const ICalEvent& ie : instances) {
        ExternalEvent e;
        e.uid = ie.uid;
        e.summary = ie.summary;
        e.location = ie.location;
        e.start = ie.start;
        e.end = ie.end;
        e.allDay = ie.allDay;
        events.append(e);
    }

    m_store.replaceSourceEvents(src.id, events);
    m_store.setSourceSynced(src.id, now);
    emit refreshed();
}

} // namespace zb
