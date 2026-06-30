#include "SymbolDatabase.h"
#include "LanguageDetector.h"
#include "CodeParser.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QCryptographicHash>
#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QDebug>
#include <QMutexLocker>
#include <QThread>
#include <QSqlDatabase>

SymbolDatabase::SymbolDatabase(const QString &dbPath, QObject *parent)
    : QObject(parent), m_dbPath(dbPath)
{
    if (m_dbPath.isEmpty())
        m_dbPath = QDir::currentPath() + "/symbols.db";
}

SymbolDatabase::~SymbolDatabase()
{
    QMutexLocker locker(&m_mutex);
    if (m_db.isOpen()) {
        m_db.close();
        QString connectionName = m_db.connectionName();
        if (!connectionName.isEmpty()) {
            QSqlDatabase::removeDatabase(connectionName);
        }
    }
}

bool SymbolDatabase::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_ready)
        return true;

    m_db = QSqlDatabase::addDatabase("QSQLITE", "symbol_db");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        emit dbError("Cannot open symbol database: " + m_db.lastError().text());
        return false;
    }

    if (!createTables()) {
        emit dbError("Failed to create tables: " + m_db.lastError().text());
        return false;
    }

    m_ready = true;
    return true;
}

bool SymbolDatabase::isReady() const
{
    QMutexLocker locker(&m_mutex);
    return m_ready;
}

bool SymbolDatabase::removeFile(const QString &filePath)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return false;

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM symbols WHERE file_path = ?");
    query.addBindValue(filePath);
    if (!query.exec()) {
        emit dbError(query.lastError().text());
        return false;
    }
    return true;
}

bool SymbolDatabase::createTables()
{
    QSqlQuery query(m_db);
    QString sql = R"(
        CREATE TABLE IF NOT EXISTS symbols (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            language TEXT NOT NULL,
            file_path TEXT NOT NULL,
            symbol_type TEXT NOT NULL,
            symbol_name TEXT NOT NULL,
            namespace TEXT DEFAULT '',
            class_name TEXT DEFAULT '',
            function_signature TEXT DEFAULT '',
            return_type TEXT DEFAULT '',
            parameters TEXT DEFAULT '',
            start_line INTEGER DEFAULT 0,
            end_line INTEGER DEFAULT 0,
            parent_symbol TEXT DEFAULT '',
            documentation TEXT DEFAULT '',
            file_hash TEXT DEFAULT '',
            last_modified INTEGER DEFAULT 0
        )
    )";

    if (!query.exec(sql)) {
        emit dbError(query.lastError().text());
        return false;
    }

    sql = R"(
        CREATE INDEX IF NOT EXISTS idx_symbols_name ON symbols(symbol_name);
        CREATE INDEX IF NOT EXISTS idx_symbols_file ON symbols(file_path);
        CREATE INDEX IF NOT EXISTS idx_symbols_type ON symbols(symbol_type);
        CREATE INDEX IF NOT EXISTS idx_symbols_lang ON symbols(language);
    )";

    if (!query.exec(sql)) {
        emit dbError(query.lastError().text());
        return false;
    }

    return true;
}

// بازنویسی با استفاده از تراکنش (Transaction) برای سرعت انفجاری
int SymbolDatabase::indexFile(const QString &filePath, const QList<SymbolInfo> &symbols, const QString &fileHash) {
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return -1;

    // شروع تراکنش: تمام عملیات‌ها در حافظه موقت انجام شده و یکباره روی هارد نوشته می‌شوند
    m_db.transaction(); 

    try {
        removeFile(filePath); // حذف اطلاعات قدیمی فایل

        QSqlQuery query(m_db);
        query.prepare(R"(
            INSERT INTO symbols (language, file_path, symbol_type, symbol_name, start_line, end_line)
            VALUES (?, ?, ?, ?, ?, ?)
        )");

        int count = 0;
        for (const SymbolInfo &sym : symbols) {
            query.addBindValue(static_cast<int>(LanguageDetector::Unknown));
            query.addBindValue(filePath);
            query.addBindValue(static_cast<int>(sym.type));
            query.addBindValue(sym.name);
            query.addBindValue(sym.startLine);
            query.addBindValue(sym.endLine);
            
            if (query.exec()) count++;
        }

        m_db.commit(); // پایان تراکنش و نوشتن روی هارد
        return count;
    } catch (...) {
        m_db.rollback(); // در صورت خطا، دیتابیس به حالت قبل برمی‌گرده و خراب نمیشه
        return -1;
    }
}

int SymbolDatabase::indexSymbol(const SymbolRecord &record)
{
    if (!ensureConnection()) return -1;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO symbols (
            language, file_path, symbol_type, symbol_name, namespace,
            class_name, function_signature, return_type, parameters,
            start_line, end_line, parent_symbol, documentation, file_hash, last_modified
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");

    query.addBindValue(static_cast<int>(record.language));
    query.addBindValue(record.filePath);
    query.addBindValue(static_cast<int>(record.symbolType));
    query.addBindValue(record.symbolName);
    query.addBindValue(record.namespace_);
    query.addBindValue(record.className);
    query.addBindValue(record.functionSignature);
    query.addBindValue(record.returnType);
    query.addBindValue(record.parameters);
    query.addBindValue(record.startLine);
    query.addBindValue(record.endLine);
    query.addBindValue(record.parentSymbol);
    query.addBindValue(record.documentation);
    query.addBindValue(record.fileHash);
    query.addBindValue(record.lastModified);

    if (!query.exec()) {
        emit dbError(query.lastError().text());
        return -1;
    }

    int id = query.lastInsertId().toInt();
    return id;
}

// بازنویسی برای مدیریت امن دیتابیس در محیط چندرشته‌ای (Thread-Safe DB)
bool SymbolDatabase::ensureConnection() {
    // ایجاد نام اتصال منحصربه‌فرد برای هر رشته (Thread)
    QString connectionName = QString("conn_%1").arg(quintptr(QThread::currentThreadId()));

    if (QSqlDatabase::contains(connectionName)) {
        m_db = QSqlDatabase::database(connectionName);
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        m_db.setDatabaseName(m_dbPath);
    }

    if (!m_db.isOpen()) {
        if (!m_db.open()) {
            emit dbError("Database thread connection failed: " + m_db.lastError().text());
            return false;
        }
        // اگر اتصال جدید است، ایندکس‌ها و تنظیمات پرفورمنس را اعمال کن
        QSqlQuery q(m_db);
        q.exec("PRAGMA journal_mode = WAL;"); // افزایش سرعت نوشتن همزمان
        q.exec("PRAGMA synchronous = NORMAL;");
    }
    return true;
}

bool SymbolDatabase::removeSymbol(int id)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return false;

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM symbols WHERE id = ?");
    query.addBindValue(id);
    return query.exec();
}

void SymbolDatabase::clear()
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return;

    QSqlQuery query(m_db);
    query.exec("DELETE FROM symbols");
}

SymbolDatabase::SymbolRecord SymbolDatabase::findSymbol(const QString &name)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return {};

    QSqlQuery query(m_db);
    query.prepare(R"(SELECT * FROM symbols WHERE symbol_name = ? COLLATE NOCASE LIMIT 1)");
    query.addBindValue(name);
    if (query.exec() && query.next())
        return queryToRecord(query);
    return {};
}

QList<SymbolDatabase::SymbolRecord> SymbolDatabase::searchSymbols(const QString &query)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return {};

    QList<SymbolRecord> results;
    QSqlQuery q(m_db);
    q.prepare(R"(SELECT * FROM symbols WHERE 
        symbol_name LIKE ? 
        OR function_signature LIKE ? 
        OR documentation LIKE ?
        LIMIT 50)");
    QString pattern = "%" + query + "%";
    q.addBindValue(pattern);
    q.addBindValue(pattern);
    q.addBindValue(pattern);

    if (q.exec()) {
        while (q.next())
            results.append(queryToRecord(q));
    }
    return results;
}

QList<SymbolDatabase::SymbolRecord> SymbolDatabase::searchByType(SymbolInfo::Type type)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return {};

    QList<SymbolRecord> results;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM symbols WHERE symbol_type = ?");
    query.addBindValue(static_cast<int>(type));
    while (query.next())
        results.append(queryToRecord(query));
    return results;
}

QList<SymbolDatabase::SymbolRecord> SymbolDatabase::searchByFile(const QString &filePath)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return {};

    QList<SymbolRecord> results;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM symbols WHERE file_path = ?");
    query.addBindValue(filePath);
    while (query.next())
        results.append(queryToRecord(query));
    return results;
}

QList<SymbolDatabase::SymbolRecord> SymbolDatabase::searchByLanguage(LanguageDetector::Language lang)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return {};

    QList<SymbolRecord> results;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM symbols WHERE language = ?");
    query.addBindValue(static_cast<int>(lang));
    while (query.next())
        results.append(queryToRecord(query));
    return results;
}

QList<SymbolDatabase::SymbolRecord> SymbolDatabase::searchByNamePrefix(const QString &prefix)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return {};

    QList<SymbolRecord> results;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM symbols WHERE symbol_name LIKE ? LIMIT 30");
    query.addBindValue(prefix + "%");
    while (query.next())
        results.append(queryToRecord(query));
    return results;
}

QList<SymbolDatabase::SymbolRecord> SymbolDatabase::getDefinitions(const QString &name)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return {};

    QList<SymbolRecord> results;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM symbols WHERE symbol_name = ? AND symbol_type IN (1,2,3,4,5)");
    query.addBindValue(name);
    while (query.next())
        results.append(queryToRecord(query));
    return results;
}

QList<SymbolDatabase::SymbolRecord> SymbolDatabase::getDeclarations(const QString &name)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return {};

    QList<SymbolRecord> results;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM symbols WHERE symbol_name = ? AND symbol_type = 5");
    query.addBindValue(name);
    while (query.next())
        results.append(queryToRecord(query));
    return results;
}

QList<SymbolDatabase::SymbolRecord> SymbolDatabase::getReferences(const QString &name)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return {};

    QList<SymbolRecord> results;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM symbols WHERE function_signature LIKE ? OR documentation LIKE ?");
    query.addBindValue("%" + name + "%");
    query.addBindValue("%" + name + "%");
    while (query.next())
        results.append(queryToRecord(query));
    return results;
}

QList<SymbolDatabase::SymbolRecord> SymbolDatabase::suggest(const QString &partial)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return {};

    QList<SymbolRecord> results;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM symbols WHERE symbol_name LIKE ? LIMIT 10");
    query.addBindValue(partial + "%");
    while (query.next())
        results.append(queryToRecord(query));
    return results;
}

int SymbolDatabase::totalSymbols() const
{
    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.exec("SELECT COUNT(*) FROM symbols");
    if (query.next())
        return query.value(0).toInt();
    return 0;
}

int SymbolDatabase::totalFiles() const
{
    QMutexLocker locker(&m_mutex);
    QSqlQuery query(m_db);
    query.exec("SELECT COUNT(DISTINCT file_path) FROM symbols");
    if (query.next())
        return query.value(0).toInt();
    return 0;
}

SymbolDatabase::SymbolRecord SymbolDatabase::queryToRecord(const QSqlQuery &query) const
{
    SymbolRecord rec;
    rec.id = query.value("id").toInt();
    rec.language = static_cast<LanguageDetector::Language>(query.value("language").toInt());
    rec.filePath = query.value("file_path").toString();
    rec.symbolType = static_cast<SymbolInfo::Type>(query.value("symbol_type").toInt());
    rec.symbolName = query.value("symbol_name").toString();
    rec.namespace_ = query.value("namespace").toString();
    rec.className = query.value("class_name").toString();
    rec.functionSignature = query.value("function_signature").toString();
    rec.returnType = query.value("return_type").toString();
    rec.parameters = query.value("parameters").toString();
    rec.startLine = query.value("start_line").toInt();
    rec.endLine = query.value("end_line").toInt();
    rec.parentSymbol = query.value("parent_symbol").toString();
    rec.documentation = query.value("documentation").toString();
    rec.fileHash = query.value("file_hash").toString();
    rec.lastModified = query.value("last_modified").toLongLong();
    return rec;
}