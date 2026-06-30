#ifndef PROJECTINDEXER_H
#define PROJECTINDEXER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <QMutex>

#include "CodeIndexer.h"
#include "LanguageDetector.h"
#include "CodeParser.h"
#include "SymbolDatabase.h"
#include "CallGraph.h"
#include "DependencyGraph.h"
#include "VectorStorageManager.h"
#include "Retriever.h"
#include "embedding_client.h"
#include "api_client.h"

class ProjectIndexer : public QObject
{
    Q_OBJECT
public:
    struct IndexProgress {
        int totalFiles = 0;
        int processedFiles = 0;
        int totalChunks = 0;
        int processedChunks = 0;
        QString currentFile;
        bool isIndexing = false;
    };

    explicit ProjectIndexer(LanguageDetector *detector, CodeParser *parser,
                            SymbolDatabase *symbolDb, CallGraph *callGraph,
                            DependencyGraph *depGraph, CodeIndexer *codeIndexer,
                            VectorStorageManager *vectorStore, Retriever *retriever,
                            EmbeddingClient *embedder, ApiClient *api,
                            QObject *parent = nullptr);
    ~ProjectIndexer() override;

    void setProjectDir(const QString &dir);
    QString projectDir() const;

    void startFullIndexing();
    void stopIndexing();
    bool isIndexing() const;

    void indexFile(const QString &filePath);
    void removeFile(const QString &filePath);

    IndexProgress currentProgress() const;

    int totalIndexedFiles() const;
    int totalIndexedSymbols() const;
    int totalIndexedChunks() const;

signals:
    void indexingStarted();
    void indexingFinished();
    void indexingProgress(const IndexProgress &progress);
    void fileIndexed(const QString &filePath, int chunks, bool updated);
    void indexingError(const QString &filePath, const QString &error);

private slots:
    void onCodeIndexerProgress(const QString &filePath, int current, int total);
    void onCodeIndexerFinished();
    void onCodeIndexerFileIndexed(const QString &filePath, int chunks, bool updated);

private:
    void initialize();
    void connectModules();
    void processIndexedChunk(const CodeIndexer::CodeChunk &chunk);
    void updateVectorStore(const CodeIndexer::CodeChunk &chunk);

    LanguageDetector *m_detector = nullptr;
    CodeParser *m_parser = nullptr;
    SymbolDatabase *m_symbolDb = nullptr;
    CallGraph *m_callGraph = nullptr;
    DependencyGraph *m_depGraph = nullptr;
    CodeIndexer *m_codeIndexer = nullptr;
    VectorStorageManager *m_vectorStore = nullptr;
    Retriever *m_retriever = nullptr;
    EmbeddingClient *m_embedder = nullptr;
    ApiClient *m_api = nullptr;

    QString m_projectDir;
    IndexProgress m_progress;
    mutable QMutex m_mutex;
    bool m_initialized = false;
};

#endif // PROJECTINDEXER_H
