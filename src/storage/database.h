#pragma once

// Owns one SQLite connection and brings the schema up to date. The connection
// is named uniquely so multiple Database instances (e.g. tests) can coexist.

#include <QSqlDatabase>
#include <QString>

namespace zb {

class Database {
public:
    Database() = default;
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Open (creating if needed) a SQLite db at `path`; ":memory:" is allowed.
    // Enables foreign keys and runs migrations. Returns false and sets *error
    // on failure.
    bool open(const QString& path, QString* error = nullptr);
    void close();

    bool isOpen() const { return m_db.isOpen(); }
    QSqlDatabase& handle() { return m_db; }

    // Current schema version (PRAGMA user_version).
    int schemaVersion();

private:
    bool migrate(QString* error);

    QSqlDatabase m_db;
    QString      m_connName;
};

} // namespace zb
