#ifndef VECTOR_DB_H
#define VECTOR_DB_H

#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QHash>
#include <QAtomicInt>
#include <QCache>
#include <QFuture>
#include <QFutureWatcher>
#include <QDateTime>
#include <QtConcurrent/QtConcurrent>
#include <QDebug>

//=============================================================================
// Data Structures
//=============================================================================

/**
 * @brief Represents a chunk of a document with its embedding and metadata
 */
struct DocumentChunk {
    int id = -1;
    int documentId = -1;
    int chunkIndex = -1;
    double similarity = 0.0;
    QString filename;
    QString text;
    QVector<float> embedding;

    // ===== از نسخه قدیمی: Metadata کامل =====
    struct Metadata {
        int pageNumber = 0;
        QString section;
        QString subsection;
        QString heading;
        bool isTable = false;
        bool isCodeBlock = false;
        bool isFeatureList = false;
        bool isSummary = false;
        QStringList keywords;
        int priority = 10;
    } metadata;

    QString cacheKey() const {
        return QString("%1_%2").arg(documentId).arg(chunkIndex);
    }

    bool isFromPrioritySection(const QStringList &prioritySections) const {
        for (const QString &section : prioritySections) {
            if (metadata.section.contains(section, Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }
};

/**
 * @brief Document metadata information
 */
struct DocumentInfo {
    int id = -1;
    int totalChunks = 0;
    QString filename;
    QString filepath;
    QString addedDate;
};

//=============================================================================
// Main VectorDB Class
//=============================================================================

/**
 * @brief Vector Database for RAG (Retrieval-Augmented Generation)
 *
 * Thread-safe implementation with proper mutex handling.
 * All public methods are thread-safe.
 */
class VectorDB : public QObject
{
    Q_OBJECT

public:
    explicit VectorDB(QObject *parent = nullptr);
    ~VectorDB() override;

    //-----------------------------------------------------------------------
    // Initialization
    //-----------------------------------------------------------------------

    bool initialize(const QString &dbPath = QString());
    bool isReady() const;

    //-----------------------------------------------------------------------
    // Document Management
    //-----------------------------------------------------------------------

    int addDocument(const QString &filename, const QString &filepath = QString());
    bool removeDocument(int documentId);
    bool removeDocumentByName(const QString &filename);
    QList<DocumentInfo> listDocuments() const;

    //-----------------------------------------------------------------------
    // Chunk Management
    //-----------------------------------------------------------------------

    bool storeChunk(int documentId, int chunkIndex,
                    const QString &text, const QVector<float> &embedding,
                    const DocumentChunk::Metadata &metadata = DocumentChunk::Metadata());
    QList<DocumentChunk> getChunks(int documentId) const;
    QList<DocumentChunk> getNeighborChunks(
        int documentId,
        int chunkIndex,
        int radius = 1) const;

    //-----------------------------------------------------------------------
    // Search Operations
    //-----------------------------------------------------------------------

    /**
     * @brief Standard search
     */
    QList<DocumentChunk> search(
        const QString &queryText,
        const QVector<float> &queryVector,
        int topK);

    /**
     * @brief Search with priority sections and validation
     */
    QList<DocumentChunk> searchWithPriority(
        const QString &queryText,
        const QVector<float> &queryVector,
        int topK,
        const QStringList &prioritySections = {},
        const QStringList &negativeKeywords = {});

    /**
     * @brief Search with reranking and automatic retry
     */
    QList<DocumentChunk> searchWithRerank(
        const QString &queryText,
        const QVector<float> &queryVector,
        int topK,
        const QStringList &prioritySections = {},
        const QStringList &negativeKeywords = {});

    /**
     * @brief Async search with priority
     */
    QFuture<QList<DocumentChunk>> searchAsync(
        const QString &queryText,
        const QVector<float> &queryVector,
        int topK);

    /**
     * @brief Get context for RAG
     */
    QString getContext(
        const QString &query,
        const QVector<float> &queryVector,
        int topK = 5,
        int maxTokens = 2000);

    //-----------------------------------------------------------------------
    // Utility Functions
    //-----------------------------------------------------------------------

    static double computeCosineSimilarity(
        const QVector<float> &a,
        const QVector<float> &b);

    int totalChunkCount() const;
    void clearAll();

signals:
    void dbError(const QString &error);
    void progress(int current, int total);
    void searchComplete(const QList<DocumentChunk> &results);

private:
    //-----------------------------------------------------------------------
    // Configuration
    //-----------------------------------------------------------------------

    struct Config {
        // ─── Search parameters ───
        int maxCandidates = 10000;
        int maxFullScanResults = 5000;
        double minSimilarity = 0.35;
        double keywordWeight = 0.25;
        double semanticWeight = 0.75;

        // ─── Cache ───
        int cacheSize = 100;
        bool enableCaching = true;

        // ─── Expansion ───
        int defaultExpansionRadius = 1;

        // ─── Async ───
        bool enableAsyncSearch = true;

        // ─── Priority Search ─── از نسخه قدیمی
        bool enableSectionPrioritization = true;
        bool enableValidation = true;
        double minConfidenceThreshold = 0.6;
        double prioritySectionBoost = 1.2;
        int maxChunksPerSection = 100;

        // ─── Reranking and Retry ─── از نسخه قدیمی
        int rerankTopK = 5;
        int maxRetrievalAttempts = 3;
        double similarityThreshold = 0.4;
        bool enableReranking = true;
        bool enableRetry = true;
        double retryThreshold = 0.3;
        int maxRetryResults = 50;

        // ─── Section priorities ─── از نسخه قدیمی
        QMap<QString, int> sectionPriorityMap;
    };

    Config m_config;

    //-----------------------------------------------------------------------
    // Database Helpers
    //-----------------------------------------------------------------------

    bool createTables();
    bool ensureConnection();
    void updateTableSchema();

    //-----------------------------------------------------------------------
    // Search Helpers
    //-----------------------------------------------------------------------

    QList<DocumentChunk> fullScanSearch(
        const QVector<float> &queryVector,
        int limit);

    QList<DocumentChunk> approximateSearch(
        const QVector<float> &queryVector,
        int limit);

    QList<DocumentChunk> searchBySection(
        const QString &queryText,
        const QVector<float> &queryVector,
        int topK,
        const QString &section);

    /**
     * @brief Validate chunks against negative keywords
     */
    QList<DocumentChunk> validateChunks(
        const QList<DocumentChunk> &chunks,
        const QString &entity,
        const QStringList &negativeKeywords);

    /**
     * @brief Rerank chunks based on multiple factors
     */
    QList<DocumentChunk> rerankChunks(
        const QList<DocumentChunk> &chunks,
        const QString &query,
        const QStringList &prioritySections = {});

    /**
     * @brief Retry search with different strategy
     */
    QList<DocumentChunk> retrySearch(
        const QString &queryText,
        const QVector<float> &queryVector,
        int topK,
        int attempt,
        const QStringList &prioritySections = {});

    /**
     * @brief Calculate confidence score for a chunk
     */
    double calculateChunkConfidence(
        const DocumentChunk &chunk,
        const QString &entity,
        const QStringList &negativeKeywords);

    double computeKeywordScoreFast(
        const QSet<QString> &queryTokens,
        const QString &text) const;

    double computeKeywordScore(
        const QString &query,
        const QString &chunk) const;

    QStringList extractKeywords(const QString &text) const;

    void expandNeighbors(
        const QList<DocumentChunk> &hits,
        QHash<QString, DocumentChunk> &mergedMap,
        int topK,
        int expansionRadius) const;

    void applyDiversityFiltering(
        QList<DocumentChunk> &results,
        int topK) const;

    //-----------------------------------------------------------------------
    // Cache Management
    //-----------------------------------------------------------------------

    struct CacheEntry {
        QList<DocumentChunk> results;
        qint64 timestamp;
    };

    mutable QCache<QString, CacheEntry> m_cache;
    mutable QMutex m_cacheMutex;

    QString getCacheKey(const QString &query, int topK) const;
    bool getFromCache(const QString &key, QList<DocumentChunk> &results) const;
    void addToCache(const QString &key, const QList<DocumentChunk> &results);

    //-----------------------------------------------------------------------
    // Threading
    //-----------------------------------------------------------------------

    mutable QMutex m_dbMutex;
    QAtomicInt m_ready;
    QFutureWatcher<QList<DocumentChunk>> *m_searchWatcher = nullptr;

    //-----------------------------------------------------------------------
    // Database Members
    //-----------------------------------------------------------------------

    QSqlDatabase m_db;
    QString m_dbPath;
    int m_dimension = 768;

    //-----------------------------------------------------------------------
    // Performance Monitoring
    //-----------------------------------------------------------------------

    struct Stats {
        QAtomicInt totalQueries;
        QAtomicInt cacheHits;
        QAtomicInt cacheMisses;
        QAtomicInt validationRejections;  // از نسخه قدیمی
        QAtomicInt prioritySectionHits;   // از نسخه قدیمی
        QAtomicInt retryAttempts;         // از نسخه قدیمی
        QAtomicInt rerankedChunks;        // از نسخه قدیمی
        double avgSearchTimeMs = 0.0;
        double avgRerankTimeMs = 0.0;     // از نسخه قدیمی
    } m_stats;

    //-----------------------------------------------------------------------
    // Helper Methods
    //-----------------------------------------------------------------------

    void initSectionPriorityMap();        // از نسخه قدیمی
    int getSectionPriority(const QString &section) const;  // از نسخه قدیمی
    bool isHighPrioritySection(const QString &section) const;  // از نسخه قدیمی
};

//=============================================================================
// Inline Helper Functions
//=============================================================================

inline bool operator==(const DocumentChunk &a, const DocumentChunk &b) {
    return a.documentId == b.documentId && a.chunkIndex == b.chunkIndex;
}

inline uint qHash(const DocumentChunk &chunk, uint seed = 0) {
    return qHash(chunk.cacheKey(), seed);
}

#endif // VECTOR_DB_H