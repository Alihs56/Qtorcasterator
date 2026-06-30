#include "pdf_processor.h"
#include "vector_db.h"
#include "embedding_client.h"
#include "logger.h"
#include <QProcess>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QSharedPointer>
#include <functional>
#include <QTimer>
#include "smart_chunker.h"

PdfProcessor::PdfProcessor(VectorDB *db, EmbeddingClient *embedder, QObject *parent)
    : QObject(parent), m_db(db), m_embedder(embedder)
{
    LOG_INFO("PDF", "PdfProcessor initialized");
    
    // ✅ ایجاد QTimer واحد
    m_processTimer = new QTimer(this);
    m_processTimer->setSingleShot(true);
    m_processTimer->setInterval(10);
    connect(m_processTimer, &QTimer::timeout, this, &PdfProcessor::processNextChunk);
}


void PdfProcessor::setChunkSize(int tokens) {
    m_chunkTokens = qMax(100, tokens);
    LOG_INFO("PDF", QString("Chunk size set to %1 tokens").arg(m_chunkTokens));
}

void PdfProcessor::setChunkOverlap(double fraction) {
    m_chunkOverlap = qBound(0.0, fraction, 0.5);
    LOG_INFO("PDF", QString("Chunk overlap set to %1%").arg(m_chunkOverlap * 100));
}

int PdfProcessor::chunkSize() const { return m_chunkTokens; }
double PdfProcessor::chunkOverlap() const { return m_chunkOverlap; }

int PdfProcessor::estimateTokenCount(const QString &text) {
    return text.length() / 4;
}

QString PdfProcessor::extractTextFromPdf(const QString &filepath) {
    QProcess proc;
    proc.start("pdftotext", {"-layout", filepath, "-"});
    if (proc.waitForFinished(30000)) {
        return QString::fromUtf8(proc.readAllStandardOutput());
    }
    return {};
}

void PdfProcessor::processPdf(const QString &filepath,
                              std::function<void(const PdfProcessingResult&)> callback) {
    QFileInfo fi(filepath);
    QString filename = fi.fileName();

    emit processingStarted(filename);
    LOG_INFO("PDFProcessor", QString("Processing: %1").arg(filename));

    auto result = QSharedPointer<PdfProcessingResult>::create();
    result->filename = filename;
    result->filepath = filepath;

    QString text = extractTextFromPdf(filepath);
    if (text.isEmpty()) {
        result->success = false;
        result->errorMessage = "Failed to extract text from PDF";
        LOG_ERROR("PDFProcessor", result->errorMessage + ": " + filename);
        emit processingError(filename, result->errorMessage);
        if (callback) callback(*result);
        emit processingFinished(*result);
        return;
    }

    result->totalPages = text.count('\f');
    if (result->totalPages == 0) result->totalPages = 1;

    SmartChunker chunker;
    QList<Chunk> chunks = chunker.build(text, m_chunkTokens, m_chunkOverlap);
    result->totalChunks = chunks.size();

    LOG_INFO("PDFProcessor", QString("%1: %2 pages, %3 chunks")
                 .arg(filename).arg(result->totalPages).arg(result->totalChunks));

    if (chunks.isEmpty()) {
        result->success = true;
        if (callback) callback(*result);
        emit processingFinished(*result);
        return;
    }

    int docId = m_db->addDocument(filename, filepath);
    if (docId < 0) {
        result->success = false;
        result->errorMessage = "Failed to add document to database";
        if (callback) callback(*result);
        emit processingFinished(*result);
        LOG_ERROR("PDF", QString("Failed to add document: %1").arg(filename));
        return;
    }

    LOG_INFO("PDF", QString("Document ID = %1").arg(docId));

    m_pdfChunks = chunks;
    m_pdfIndex = 0;
    m_pdfDocId = docId;
    m_pdfResult = result;
    m_pdfCallback = callback;
    m_pdfChunkState = QSharedPointer<PdfChunkState>::create();

    QTimer::singleShot(0, this, SLOT(processNextChunk()));
    m_processTimer->start();
}

void PdfProcessor::storeChunkResult(const Chunk &chunk, const QVector<float> &vec, const QString &text) {
    if (vec.isEmpty()) {
        m_pdfChunkState->errors++;
        LOG_WARN("PDF", QString("Empty embedding for chunk %1").arg(chunk.chunkIndex));
    } else {
        m_db->storeChunk(m_pdfDocId, chunk.chunkIndex, text, vec, m_pdfMetadata);
        LOG_INFO("PDF", QString("✓ Chunk %1 stored successfully").arg(chunk.chunkIndex));
    }
    m_pdfChunkState->completed++;
}

// بازنویسی با مکانیزم "صف‌بندی" ایمن و مدیریت خطای پیشرفته
void PdfProcessor::processNextChunk() {
    if (m_pdfIndex >= m_pdfChunks.size()) {
        m_pdfResult->success = (m_pdfChunkState->errors < (m_pdfChunks.size() / 2));
        LOG_INFO("PDF", QString("Finished. Chunks: %1, Errors: %2").arg(m_pdfChunks.size()).arg(m_pdfChunkState->errors));
        
        if (m_pdfCallback) m_pdfCallback(*m_pdfResult);
        emit processingFinished(*m_pdfResult);
        return;
    }

    const Chunk &chunk = m_pdfChunks[m_pdfIndex];
    QString chunkText = chunk.text.simplified();

    // پاکسازی کاراکترهای غیرمجاز که باعث خطای Embedding می‌شوند
    chunkText.remove(QRegularExpression(R"([^\x20-\x7E])"));

    m_embedder->getEmbedding(chunkText, [this, chunk, chunkText](const QVector<float> &vec) {
        if (vec.isEmpty()) {
            m_pdfChunkState->errors++;
            LOG_WARN("PDF", "Failed embedding for chunk " + QString::number(chunk.chunkIndex));
        } else {
            m_pdfMetadata.pageNumber = chunk.pageNumber;
            m_pdfMetadata.section = chunk.section;
            m_db->storeChunk(m_pdfDocId, chunk.chunkIndex, chunkText, vec, m_pdfMetadata);
        }

        m_pdfChunkState->completed++;
        m_pdfIndex++;
        
        // تولید سیگنال پیشرفت برای UI
        emit processingProgress(m_pdfResult->filename, m_pdfIndex, m_pdfChunks.size());

        // استفاده از تایمر برای اجازه دادن به EventLoop جهت پردازش سایر رویدادها (جلوگیری از فریز)
        QTimer::singleShot(5, this, &PdfProcessor::processNextChunk);
    });
}