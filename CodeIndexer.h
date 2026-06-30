#ifndef CODEINDEXER_H
#define CODEINDEXER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QFileSystemWatcher>
#include <QFileInfo>
#include <QFile>
#include <QCryptographicHash>
#include <QHash>
#include <QMutex>
#include <QTimer>
#include <QSet>
#include <functional>

#include "LanguageDetector.h"
#include "CodeParser.h"
#include "SymbolDatabase.h"

class CodeIndexer : public QObject
{
    Q_OBJECT
public:
    struct CodeChunk {
        QString filePath;
        QString language;
        QString className;
        QString functionName;
        QString symbolId;
        QString chunkType;
        int startLine = 0;
        int endLine = 0;
        QString text;
        QString hash;
        int tokenCount = 0;
    };

    explicit CodeIndexer(LanguageDetector *detector, CodeParser *parser,
                         SymbolDatabase *symbolDb, QObject *parent = nullptr);
    ~CodeIndexer() override;

    void setProjectDir(const QString &dir);
    void startIndexing();
    void stopIndexing();
    bool isIndexing() const;

    QList<CodeChunk> chunkFile(const QString &filePath);
    QList<CodeChunk> chunkContent(const QString &content, const QString &filePath,
                                  LanguageDetector::Language lang);
    void reindexFile(const QString &filePath);
    void removeFile(const QString &filePath);

    int totalChunks() const;
    int totalFilesIndexed() const;
    QStringList watchedFiles() const;

    QString fileHash(const QString &filePath) const;
    bool hasChanged(const QString &filePath) const;

signals:
    void indexingStarted();
    void indexingFinished();
    void indexingProgress(const QString &filePath, int current, int total);
    void fileIndexed(const QString &filePath, int chunks, bool updated);
    void fileChunked(const CodeChunk &chunk);
    void indexingError(const QString &filePath, const QString &error);

private slots:
    void onFileChanged(const QString &path);
    void onDirChanged(const QString &path);

private:
    void scanAndIndex();
    void indexFile(const QString &filePath);
    void debounceChanged(const QString &path);

    LanguageDetector *m_detector = nullptr;
    CodeParser *m_parser = nullptr;
    SymbolDatabase *m_symbolDb = nullptr;

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounceTimer = nullptr;
    QHash<QString, QString> m_fileHashes;
    QHash<QString, QList<CodeChunk>> m_fileChunks;
    QSet<QString> m_changedFiles;
    QString m_projectDir;

    mutable QMutex m_mutex;
    bool m_indexing = false;
    int m_totalFiles = 0;
    int m_totalChunks = 0;
};

#endif // CODEINDEXER_H
