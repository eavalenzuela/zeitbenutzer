#include "storage/database.h"

#include <QAtomicInteger>
#include <QSqlError>
#include <QSqlQuery>

namespace zb {

namespace {

QString nextConnectionName()
{
    static QAtomicInteger<quint64> counter{0};
    return QStringLiteral("zb_conn_%1").arg(counter.fetchAndAddRelaxed(1));
}

// Schema version this build expects. Bump + add a branch in migrate() to evolve.
constexpr int kSchemaVersion = 1;

// Full v1 schema. Applied in one transaction when user_version == 0.
const char* const kSchemaV1 = R"SQL(
CREATE TABLE project (
    id        INTEGER PRIMARY KEY,
    parent_id INTEGER REFERENCES project(id) ON DELETE CASCADE,
    name      TEXT    NOT NULL,
    color     TEXT    NOT NULL DEFAULT '',
    archived  INTEGER NOT NULL DEFAULT 0,
    sort      INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE note (
    id         INTEGER PRIMARY KEY,
    project_id INTEGER NOT NULL REFERENCES project(id) ON DELETE CASCADE,
    title      TEXT    NOT NULL,
    body_md    TEXT    NOT NULL DEFAULT '',
    created_at INTEGER NOT NULL,
    updated_at INTEGER NOT NULL
);

CREATE TABLE task_series (
    id           INTEGER PRIMARY KEY,
    project_id   INTEGER NOT NULL REFERENCES project(id) ON DELETE CASCADE,
    title        TEXT    NOT NULL,
    estimate_min INTEGER NOT NULL DEFAULT 0,
    rrule        TEXT    NOT NULL,
    anchor_due   INTEGER NOT NULL,
    until        INTEGER
);

CREATE TABLE task (
    id              INTEGER PRIMARY KEY,
    project_id      INTEGER NOT NULL REFERENCES project(id) ON DELETE CASCADE,
    series_id       INTEGER REFERENCES task_series(id) ON DELETE SET NULL,
    occurrence_date TEXT,
    title           TEXT    NOT NULL,
    status          INTEGER NOT NULL DEFAULT 0,
    estimate_min    INTEGER NOT NULL DEFAULT 0,
    due             INTEGER,
    completed_at    INTEGER,
    sort            INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE block_series (
    id           INTEGER PRIMARY KEY,
    project_id   INTEGER REFERENCES project(id) ON DELETE SET NULL,
    title        TEXT    NOT NULL,
    color        TEXT    NOT NULL DEFAULT '',
    rrule        TEXT    NOT NULL,
    anchor_start INTEGER NOT NULL,
    anchor_end   INTEGER NOT NULL,
    until        INTEGER
);

CREATE TABLE block (
    id              INTEGER PRIMARY KEY,
    series_id       INTEGER REFERENCES block_series(id) ON DELETE SET NULL,
    occurrence_date TEXT,
    planned_start   INTEGER NOT NULL,
    planned_end     INTEGER NOT NULL,
    actual_start    INTEGER,
    actual_end      INTEGER,
    project_id      INTEGER REFERENCES project(id) ON DELETE SET NULL,
    title           TEXT    NOT NULL,
    color           TEXT    NOT NULL DEFAULT '',
    status          INTEGER NOT NULL DEFAULT 0,
    detached        INTEGER NOT NULL DEFAULT 0,
    skipped         INTEGER NOT NULL DEFAULT 0,
    carried_from    INTEGER REFERENCES block(id) ON DELETE SET NULL
);

CREATE TABLE block_task (
    block_id INTEGER NOT NULL REFERENCES block(id) ON DELETE CASCADE,
    task_id  INTEGER NOT NULL REFERENCES task(id)  ON DELETE CASCADE,
    PRIMARY KEY (block_id, task_id)
);

CREATE TABLE block_note (
    block_id INTEGER NOT NULL REFERENCES block(id) ON DELETE CASCADE,
    note_id  INTEGER NOT NULL REFERENCES note(id)  ON DELETE CASCADE,
    PRIMARY KEY (block_id, note_id)
);

CREATE INDEX idx_block_planned ON block(planned_start);
CREATE INDEX idx_block_project ON block(project_id);
CREATE INDEX idx_block_series  ON block(series_id, occurrence_date);
CREATE INDEX idx_note_project  ON note(project_id);
CREATE INDEX idx_task_project  ON task(project_id);
)SQL";

} // namespace

Database::~Database()
{
    close();
}

bool Database::open(const QString& path, QString* error)
{
    close();

    m_connName = nextConnectionName();
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connName);
    m_db.setDatabaseName(path);

    if (!m_db.open()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }

    // Foreign keys are off by default in SQLite; enable per connection.
    QSqlQuery pragma(m_db);
    if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
        if (error)
            *error = pragma.lastError().text();
        return false;
    }

    return migrate(error);
}

void Database::close()
{
    if (m_db.isOpen())
        m_db.close();
    m_db = QSqlDatabase();
    if (!m_connName.isEmpty()) {
        QSqlDatabase::removeDatabase(m_connName);
        m_connName.clear();
    }
}

int Database::schemaVersion()
{
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("PRAGMA user_version")) && q.next())
        return q.value(0).toInt();
    return -1;
}

bool Database::migrate(QString* error)
{
    const int version = schemaVersion();
    if (version >= kSchemaVersion)
        return true;

    if (!m_db.transaction()) {
        if (error)
            *error = m_db.lastError().text();
        return false;
    }

    const auto fail = [&](const QString& msg) {
        if (error)
            *error = msg;
        m_db.rollback();
        return false;
    };

    if (version < 1) {
        // exec() of QSQLITE runs a single statement; split the bootstrap script.
        const QStringList stmts =
            QString::fromUtf8(kSchemaV1).split(QLatin1Char(';'), Qt::SkipEmptyParts);
        for (const QString& raw : stmts) {
            const QString sql = raw.trimmed();
            if (sql.isEmpty())
                continue;
            QSqlQuery q(m_db);
            if (!q.exec(sql))
                return fail(q.lastError().text());
        }
    }

    // PRAGMA user_version doesn't accept a bound parameter.
    QSqlQuery setv(m_db);
    if (!setv.exec(QStringLiteral("PRAGMA user_version = %1").arg(kSchemaVersion)))
        return fail(setv.lastError().text());

    if (!m_db.commit())
        return fail(m_db.lastError().text());

    return true;
}

} // namespace zb
