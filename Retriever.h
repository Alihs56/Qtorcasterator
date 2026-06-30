#ifndef RETRIEVER_H
#define RETRIEVER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QVector>
#include <functional>
#include <memory>

#include "LanguageDetector.h"
#include "SymbolDatabase.h"
#include "CallGraph.h"
#include "DependencyGraph.h"
#include "VectorStorageManager.h"

class EmbeddingClient;

class Retriever : public QObject
{
    Q_OBJECT
public:
    struct VecRecord {
        int id = -1;
        float distance = 0.0f;
        float similarity = 0.0f;
        QString metadata;
        QString filePath;
        int chunkIndex = 0;
    };

    struct RetrievalResult {
        QString context;
        QList<QString> symbols;
        QList<QString> callGraphPaths;
        QList<QString> dependencies;
        QList<QString> chunks;
        QList<VecRecord> vectors;
        int tokensUsed = 0;
        double avgSimilarity = 0.0;
    };

    explicit Retriever(LanguageDetector *detector, SymbolDatabase *symbols, CallGraph *callGraph,
                       DependencyGraph *depGraph, VectorStorageManager *vectorStore,
                       EmbeddingClient *embedder, QObject *parent = nullptr);
    ~Retriever() override = default;

    void setEmbeddingFunction(std::function<QVector<float>(const QString&)> fn);
    void retrieve(const QString &query, bool needRag, std::function<void(const RetrievalResult&)> callback);
    void retrieveHybrid(const QString &query, int topK, std::function<void(const RetrievalResult&)> callback);
    void retrieveWithMMR(const QString &query, int topK, double lambda, std::function<void(const RetrievalResult&)> callback);

signals:
    void retrievalComplete(const RetrievalResult &result);
    void retrievalError(const QString &error);

private:
    RetrievalResult symbolSearch(const QString &query);
    RetrievalResult callGraphSearch(const QString &query);
    RetrievalResult dependencySearch(const QString &query);
    RetrievalResult semanticSearch(const QString &query, int topK);

    SymbolDatabase *m_symbols = nullptr;
    CallGraph *m_callGraph = nullptr;
    DependencyGraph *m_depGraph = nullptr;
    VectorStorageManager *m_vectorStore = nullptr;
    EmbeddingClient *m_embedder = nullptr;
    LanguageDetector *m_detector = nullptr;

    std::function<QVector<float>(const QString&)> m_embedFn;
};

#endif // RETRIEVER_H
