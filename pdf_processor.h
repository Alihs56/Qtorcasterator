#ifndef PDF_PROCESSOR_H
#define PDF_PROCESSOR_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <QSharedPointer>
#include <QTimer>

class VectorDB;
class EmbeddingClient;

#include "vector_db.h"
#include "smart_chunker.h"

struct PdfProcessingResult {
    bool success = false;
    QString filename;
    QString filepath;
    int totalChunks = 0;
    int totalPages = 0;
    QString errorMessage;
};

struct PdfChunkState {
    int completed = 0;
    int errors = 0;
};

class PdfProcessor : public QObject {
    Q_OBJECT
public:
    explicit PdfProcessor(VectorDB *db, EmbeddingClient *embedder, QObject *parent = nullptr);

    void processPdf(const QString &filepath,
                    std::function<void(const PdfProcessingResult&)> callback);

    void setChunkSize(int tokens);
    void setChunkOverlap(double fraction);
    int chunkSize() const;
    double chunkOverlap() const;

    static int estimateTokenCount(const QString &text);
    static QString extractTextFromPdf(const QString &filepath);

signals:
    void processingStarted(const QString &filename);
    void processingProgress(const QString &filename, int page, int totalPages);
    void processingFinished(const PdfProcessingResult &result);
    void processingError(const QString &filename, const QString &error);

private slots:
    void processNextChunk();

private:
    void storeChunkResult(const Chunk &chunk, const QVector<float> &vec, const QString &text);

    VectorDB *m_db;
    EmbeddingClient *m_embedder;
    int m_chunkTokens = 1000;
    double m_chunkOverlap = 0.10;

    QList<Chunk> m_pdfChunks;
    int m_pdfIndex = 0;
    int m_pdfDocId = -1;
    QSharedPointer<PdfProcessingResult> m_pdfResult;
    std::function<void(const PdfProcessingResult&)> m_pdfCallback;
    QSharedPointer<PdfChunkState> m_pdfChunkState;
    DocumentChunk::Metadata m_pdfMetadata;
    int m_pdfRetryCount = 0;
    QTimer *m_processTimer = nullptr;
};

#endif
