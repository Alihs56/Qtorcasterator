#ifndef RETRIEVAL_MANAGER_H
#define RETRIEVAL_MANAGER_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QList>
#include <functional>

class ApiClient;
class VectorDB;
class EmbeddingClient;
class PdfProcessor;
struct DocumentChunk;

/**
 * @brief Retrieval Manager — retrieves knowledge from documents.
 *
 * Responsibilities:
 * - Ingest PDFs into vector database
 * - Query understanding and expansion
 * - Hybrid search (BM25 + embedding)
 * - Context compression
 * - Never answers the user, never builds prompts
 */
class RetrievalManager : public QObject
{
    Q_OBJECT
public:
    struct RetrievalResult {
        QString context;
        QList<DocumentChunk> chunks;
        int tokensUsed = 0;
    };

    explicit RetrievalManager(ApiClient *api, VectorDB *db,
                              EmbeddingClient *embedder, PdfProcessor *pdfProc,
                              QObject *parent = nullptr);

    void retrieve(const QString &query, bool needRag,
                  std::function<void(const RetrievalResult&)> callback);

    void ingestPdf(const QString &filepath,
                   std::function<void(bool success, int chunks, int pages)> callback);

    void clear();

signals:
    void retrievalComplete(const RetrievalResult &result);
    void retrievalError(const QString &error);
    void pdfIngested(const QString &filename, int chunks, int pages);

private:
    QString expandQuery(const QString &query) const;
    QString compressContext(const QString &context, int maxTokens) const;

    ApiClient *m_api;
    VectorDB *m_db;
    EmbeddingClient *m_embedder;
    PdfProcessor *m_pdfProc;
};

#endif
