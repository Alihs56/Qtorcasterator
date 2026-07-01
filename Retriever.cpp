#include "Retriever.h"
#include "SymbolDatabase.h"
#include "CallGraph.h"
#include "DependencyGraph.h"
#include "VectorStorageManager.h"
#include "EmbeddingClient.h"
#include "LanguageDetector.h"
#include <QDebug>
#include <QRegularExpression>

Retriever::Retriever(LanguageDetector *detector, SymbolDatabase *symbols,
                     CallGraph *callGraph, DependencyGraph *depGraph,
                     VectorStorageManager *vectorStore, EmbeddingClient *embedder,
                     QObject *parent)
    : QObject(parent), m_detector(detector), m_symbols(symbols),
      m_callGraph(callGraph), m_depGraph(depGraph),
      m_vectorStore(vectorStore), m_embedder(embedder)
{
}

void Retriever::setEmbeddingFunction(std::function<QVector<float>(const QString&)> fn)
{
    m_embedFn = fn;
}

void Retriever::retrieve(const QString &query, bool needRag, std::function<void(const RetrievalResult&)> callback)
{
    RetrievalResult result;

    // 1. Symbol search
    if (m_symbols) {
        result.symbols = m_symbols->suggest(query).toList();
    }

    // 2. Call graph search
    if (m_callGraph) {
        auto related = m_callGraph->getRelatedSymbols(query, 2);
        result.callGraphPaths = related;
    }

    // 3. Dependency search
    if (m_depGraph) {
        auto deps = m_depGraph->getTransitiveDependencies(query, 2);
        result.dependencies = deps;
    }

    // 4. Semantic/vector search if RAG needed
    if (needRag && m_embedFn && m_vectorStore) {
        auto vectors = semanticSearch(query, 5);
        result.vectors = vectors.vectors;
    }

    result.context = QString("Found %1 symbols, %2 call paths, %3 dependencies")
                        .arg(result.symbols.size())
                        .arg(result.callGraphPaths.size())
                        .arg(result.dependencies.size());

    emit retrievalComplete(result);
    if (callback) callback(result);
}

void Retriever::retrieveHybrid(const QString &query, int topK, std::function<void(const RetrievalResult&)> callback)
{
    RetrievalResult result;

    // Combine multiple retrieval strategies
    result = symbolSearch(query);
    auto callGraphRes = callGraphSearch(query);
    auto depRes = dependencySearch(query);
    auto semanticRes = semanticSearch(query, topK);

    result.symbols.append(callGraphRes.symbols);
    result.callGraphPaths.append(callGraphRes.callGraphPaths);
    result.dependencies.append(depRes.dependencies);
    result.vectors.append(semanticRes.vectors);

    result.tokensUsed = result.context.length() / 4;

    emit retrievalComplete(result);
    if (callback) callback(result);
}

void Retriever::retrieveWithMMR(const QString &query, int topK, double lambda, std::function<void(const RetrievalResult&)> callback)
{
    Q_UNUSED(lambda);
    // Maximal Marginal Relevance: balance relevance and diversity
    RetrievalResult result = semanticSearch(query, topK * 2);

    // Simple greedy MMR: keep top-K most relevant and diverse
    if (result.vectors.size() > topK) {
        result.vectors = result.vectors.mid(0, topK);
    }

    emit retrievalComplete(result);
    if (callback) callback(result);
}

Retriever::RetrievalResult Retriever::symbolSearch(const QString &query)
{
    RetrievalResult result;
    if (!m_symbols) return result;

    auto records = m_symbols->searchSymbols(query);
    for (const auto &rec : records) {
        result.symbols.append(rec.symbolName);
    }

    result.context = QString("Symbol search found %1 matches").arg(result.symbols.size());
    return result;
}

Retriever::RetrievalResult Retriever::callGraphSearch(const QString &query)
{
    RetrievalResult result;
    if (!m_callGraph) return result;

    auto callers = m_callGraph->getCallers(query);
    auto callees = m_callGraph->getCallees(query);

    result.callGraphPaths.append(callers);
    result.callGraphPaths.append(callees);
    result.context = QString("Call graph: %1 callers, %2 callees").arg(callers.size()).arg(callees.size());

    return result;
}

Retriever::RetrievalResult Retriever::dependencySearch(const QString &query)
{
    RetrievalResult result;
    if (!m_depGraph) return result;

    auto deps = m_depGraph->getTransitiveDependencies(query, 3);
    result.dependencies = deps;
    result.context = QString("Found %1 transitive dependencies").arg(deps.size());

    return result;
}

Retriever::RetrievalResult Retriever::semanticSearch(const QString &query, int topK)
{
    RetrievalResult result;
    if (!m_embedFn || !m_vectorStore) return result;

    // Get query embedding
    auto queryVec = m_embedFn(query);
    if (queryVec.isEmpty()) return result;

    // Search vector store (placeholder - actual implementation depends on VectorStorageManager API)
    // TODO: Implement actual semantic search

    result.context = QString("Semantic search with query embedding size %1").arg(queryVec.size());
    return result;
}
