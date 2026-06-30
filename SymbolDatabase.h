#ifndef SYMBOLDATABASE_H
#define SYMBOLDATABASE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QMutex>
#include <functional>

#include "LanguageDetector.h"
#include "CodeParser.h"

class SymbolDatabase : public QObject
{
    Q_OBJECT
public:
    struct SymbolRecord {
        int id = -1;
        LanguageDetector::Language language = LanguageDetector::Unknown;
        QString filePath;
        SymbolInfo::Type symbolType = SymbolInfo::Unknown;
        QString symbolName;
        QString namespace_;
        QString className;
        QString functionSignature;
        QString returnType;
        QString parameters;
        int startLine = 0;
        int endLine = 0;
        QString parentSymbol;
        QString documentation;
        QString fileHash;
        qint64 lastModified = 0;
    };

    explicit SymbolDatabase(const QString &dbPath = QString(), QObject *parent = nullptr);
    ~SymbolDatabase() override;

    bool initialize();
    bool isReady() const;

    int indexFile(const QString &filePath, const QList<SymbolInfo> &symbols, const QString &fileHash);
    int indexSymbol(const SymbolRecord &record);
    bool removeFile(const QString &filePath);
    bool removeSymbol(int id);
    void clear();

    SymbolRecord findSymbol(const QString &name);
    QList<SymbolRecord> searchSymbols(const QString &query);
    QList<SymbolRecord> searchByType(SymbolInfo::Type type);
    QList<SymbolRecord> searchByFile(const QString &filePath);
    QList<SymbolRecord> searchByLanguage(LanguageDetector::Language lang);
    QList<SymbolRecord> searchByNamePrefix(const QString &prefix);
    QList<SymbolRecord> getDefinitions(const QString &name);
    QList<SymbolRecord> getDeclarations(const QString &name);
    QList<SymbolRecord> getReferences(const QString &name);
    QList<SymbolRecord> suggest(const QString &partial);

    int totalSymbols() const;
    int totalFiles() const;

signals:
    void indexingProgress(const QString &filePath, int current, int total);
    void symbolIndexed(const SymbolRecord &record);
    void dbError(const QString &error);

private:
    bool createTables();
    bool ensureConnection();
    SymbolRecord queryToRecord(const QSqlQuery &query) const;
    int countFiles() const;

    mutable QMutex m_mutex;
    QSqlDatabase m_db;
    QString m_dbPath;
    bool m_ready = false;
    int m_nextId = 1;
};

#endif // SYMBOLDATABASE_H
