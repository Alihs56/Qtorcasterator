#include "Retriever.h"
#include "LanguageDetector.h"
#include <QRegularExpression>
#include <QDebug>

Retriever::Retriever(LanguageDetector *detector, SymbolDatabase *symbols, CallGraph *callGraph,
                     DependencyGraph *depGraph, VectorStorageManager *vectorStore,
                     EmbeddingClient *embedder, QObject *parent)
    : QObject(parent),
      m_detector(detector),
      m_symbols(symbols),
      m_callGraph(callGraph),
      m_depGraph(depGraph),
      m_vectorStore(vectorStore),
      m_embedder(embedder)
{
}

void Retriever::setEmbeddingFunction(std::function<QVector<float>(const QString&)> fn)
{
    m_embedFn = fn;
}

void Retriever::retrieve(const QString &query, bool needRag, std::function<void(const RetrievalResult&)> callback)
{
    if (!needRag) {
        RetrievalResult empty;
        empty.tokensUsed = 0;
        callback(empty);
        return;
    }

    RetrievalResult result;

    RetrievalResult symRes = symbolSearch(query);
    result.symbols.append(symRes.symbols);

    RetrievalResult cgRes = callGraphSearch(query);
    result.callGraphPaths.append(cgRes.callGraphPaths);

    RetrievalResult depRes = dependencySearch(query);
    result.dependencies.append(depRes.dependencies);

    RetrievalResult vecRes = semanticSearch(query, 20);
    result.vectors.append(vecRes.vectors);

    QString context;
    for (const QString &s : result.symbols)
        context += "Symbol: " + s + "\n";
    for (const QString &s : result.callGraphPaths)
        context += "CallGraph: " + s + "\n";
    for (const QString &s : result.dependencies)
        context += "Dependency: " + s + "\n";
    for (const VecRecord &v : result.vectors)
        context += "Code: " + v.metadata + "\n";
    result.context = context;
    result.tokensUsed = context.length() / 4;

    callback(result);
}

void Retriever::retrieveHybrid(const QString &query, int topK, std::function<void(const RetrievalResult&)> callback)
{
    RetrievalResult result = semanticSearch(query, topK);
    callback(result);
}

void Retriever::retrieveWithMMR(const QString &query, int topK, double lambda, std::function<void(const RetrievalResult&)> callback)
{
    Q_UNUSED(lambda);
    retrieveHybrid(query, topK, callback);
}

Retriever::RetrievalResult Retriever::symbolSearch(const QString &query)
{
    RetrievalResult result;
    if (!m_symbols) return result;

    QList<SymbolDatabase::SymbolRecord> records = m_symbols->searchSymbols(query);
    for (const SymbolDatabase::SymbolRecord &rec : records) {
        result.symbols.append(QString("[%1] %2::%3 L%4-L%5")
                                  .arg(m_detector->languageName(rec.language))
                                  .arg(rec.className)
                                  .arg(rec.symbolName)
                                  .arg(rec.startLine)
                                  .arg(rec.endLine));
    }
    return result;
}

Retriever::RetrievalResult Retriever::callGraphSearch(const QString &query)
{
    RetrievalResult result;
    if (!m_callGraph) return result;

    QRegularExpression funcRe(R"(\b([A-Za-z_][\w:]*)::?([A-Za-z_][\w]*)\b)");
    auto match = funcRe.match(query);
    if (match.hasMatch()) {
        QString func = match.captured(0);
        QList<QString> callers = m_callGraph->getCallers(func);
        QList<QString> callees = m_callGraph->getCallees(func);
        for (const QString &c : callers)
            result.callGraphPaths.append("caller:" + c);
        for (const QString &c : callees)
            result.callGraphPaths.append("callee:" + c);
    }
    return result;
}

Retriever::RetrievalResult Retriever::dependencySearch(const QString &query)
{
    RetrievalResult result;
    if (!m_depGraph) return result;

    QList<SymbolDatabase::SymbolRecord> syms;
    Q_UNUSED(syms);
    Q_UNUSED(query);

    return result;
}

Retriever::RetrievalResult Retriever::semanticSearch(const QString &query, int topK)
{
    RetrievalResult result;
    if (!m_vectorStore) return result;

    if (m_embedFn) {
        QVector<float> vec = m_embedFn(query);
        QList<VectorStorageManager::SearchResult> vecResults = m_vectorStore->searchTopK(vec, topK);
        for (const VectorStorageManager::SearchResult &vr : vecResults) {
            VecRecord rec;
            rec.id = vr.id;
            rec.distance = vr.distance;
            rec.similarity = vr.similarity;
            rec.metadata = vr.metadata;
            rec.filePath = vr.filePath;
            rec.chunkIndex = vr.chunkIndex;
            result.vectors.append(rec);
            result.chunks.append(vr.metadata);
            if (!vr.metadata.isEmpty())
                result.context += vr.metadata + "\n";
        }
    }
    return result;
}
