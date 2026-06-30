#include "vector_db.h"
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QSet>
#include <QtMath>
#include <QRandomGenerator>

VectorDB::VectorDB(QObject *parent)
    : QObject(parent)
    , m_cache(m_config.cacheSize)
{
    m_searchWatcher = new QFutureWatcher<QList<DocumentChunk>>(this);
    connect(m_searchWatcher, &QFutureWatcher<QList<DocumentChunk>>::finished, this, [this]() {
        emit searchComplete(m_searchWatcher->result());
    });
    initSectionPriorityMap();
}

VectorDB::~VectorDB()
{
    QMutexLocker locker(&m_dbMutex);
    if (m_db.isOpen())
        m_db.close();
}

bool VectorDB::initialize(const QString &dbPath)
{
    QMutexLocker locker(&m_dbMutex);

    m_dbPath = dbPath.isEmpty() ? QCoreApplication::applicationDirPath() + "/vector_db.sqlite" : dbPath;
    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());

    m_db = QSqlDatabase::addDatabase("QSQLITE", "vector_db_connection");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        emit dbError("Failed to open vector database: " + m_db.lastError().text());
        return false;
    }

    if (!createTables()) {
        emit dbError("Failed to create vector database tables");
        return false;
    }

    updateTableSchema();
    m_ready.storeRelaxed(1);
    return true;
}

bool VectorDB::isReady() const
{
    return m_ready.loadRelaxed() == 1;
}

bool VectorDB::createTables()
{
    QSqlQuery q(m_db);

    bool ok = q.exec(
        "CREATE TABLE IF NOT EXISTS documents ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  filename TEXT NOT NULL,"
        "  filepath TEXT,"
        "  total_chunks INTEGER DEFAULT 0,"
        "  added_date TEXT DEFAULT (datetime('now'))"
        ")"
    );
    if (!ok) return false;

    ok = q.exec(
        "CREATE TABLE IF NOT EXISTS chunks ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  document_id INTEGER NOT NULL,"
        "  chunk_index INTEGER NOT NULL,"
        "  text TEXT NOT NULL,"
        "  embedding BLOB,"
        "  page_number INTEGER DEFAULT 0,"
        "  section TEXT DEFAULT '',"
        "  subsection TEXT DEFAULT '',"
        "  heading TEXT DEFAULT '',"
        "  is_table INTEGER DEFAULT 0,"
        "  is_code_block INTEGER DEFAULT 0,"
        "  is_feature_list INTEGER DEFAULT 0,"
        "  is_summary INTEGER DEFAULT 0,"
        "  keywords TEXT DEFAULT '',"
        "  priority INTEGER DEFAULT 10,"
        "  FOREIGN KEY (document_id) REFERENCES documents(id) ON DELETE CASCADE"
        ")"
    );
    if (!ok) return false;

    ok = q.exec(
        "CREATE INDEX IF NOT EXISTS idx_chunks_doc_id ON chunks(document_id)"
    );
    if (!ok) return false;

    ok = q.exec(
        "CREATE INDEX IF NOT EXISTS idx_chunks_section ON chunks(section)"
    );
    if (!ok) return false;

    return true;
}

void VectorDB::updateTableSchema()
{
    if (!m_db.isOpen()) return;

    QSqlQuery q(m_db);
    QStringList columns = m_db.tables();

    if (columns.contains("chunks")) {
        QSqlQuery pragma(m_db);
        pragma.exec("PRAGMA table_info(chunks)");
        QSet<QString> existingCols;
        while (pragma.next()) {
            existingCols.insert(pragma.value(1).toString());
        }

        struct Column { QString name, type; };
        QList<Column> needed = {
            {"page_number", "INTEGER DEFAULT 0"},
            {"section", "TEXT DEFAULT ''"},
            {"subsection", "TEXT DEFAULT ''"},
            {"heading", "TEXT DEFAULT ''"},
            {"is_table", "INTEGER DEFAULT 0"},
            {"is_code_block", "INTEGER DEFAULT 0"},
            {"is_feature_list", "INTEGER DEFAULT 0"},
            {"is_summary", "INTEGER DEFAULT 0"},
            {"keywords", "TEXT DEFAULT ''"},
            {"priority", "INTEGER DEFAULT 10"}
        };

        for (const auto &col : needed) {
            if (!existingCols.contains(col.name)) {
                QString sql = QString("ALTER TABLE chunks ADD COLUMN %1 %2").arg(col.name, col.type);
                if (!q.exec(sql)) {
                    qWarning() << "VectorDB: Failed to add column" << col.name << q.lastError().text();
                }
            }
        }
    }
}

bool VectorDB::ensureConnection()
{
    if (!m_db.isOpen()) {
        return initialize(m_dbPath);
    }
    return true;
}

// ─── Document Management ───

int VectorDB::addDocument(const QString &filename, const QString &filepath)
{
    QMutexLocker locker(&m_dbMutex);
    if (!ensureConnection()) return -1;

    QSqlQuery q(m_db);
    q.prepare("INSERT INTO documents (filename, filepath, total_chunks) VALUES (?, ?, 0)");
    q.addBindValue(filename);
    q.addBindValue(filepath.isEmpty() ? filename : filepath);
    if (!q.exec()) {
        emit dbError("Failed to add document: " + q.lastError().text());
        return -1;
    }
    return q.lastInsertId().toInt();
}

bool VectorDB::removeDocument(int documentId)
{
    QMutexLocker locker(&m_dbMutex);
    if (!ensureConnection()) return false;

    QSqlQuery q(m_db);
    q.prepare("DELETE FROM chunks WHERE document_id = ?");
    q.addBindValue(documentId);
    q.exec();

    q.prepare("DELETE FROM documents WHERE id = ?");
    q.addBindValue(documentId);
    return q.exec();
}

bool VectorDB::removeDocumentByName(const QString &filename)
{
    QMutexLocker locker(&m_dbMutex);
    if (!ensureConnection()) return false;

    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM documents WHERE filename = ?");
    q.addBindValue(filename);
    if (q.exec() && q.next()) {
        int id = q.value(0).toInt();

        QSqlQuery delChunks(m_db);
        delChunks.prepare("DELETE FROM chunks WHERE document_id = ?");
        delChunks.addBindValue(id);
        delChunks.exec();

        QSqlQuery delDoc(m_db);
        delDoc.prepare("DELETE FROM documents WHERE id = ?");
        delDoc.addBindValue(id);
        return delDoc.exec();
    }
    return false;
}

QList<DocumentInfo> VectorDB::listDocuments() const
{
    QMutexLocker locker(&m_dbMutex);
    QList<DocumentInfo> result;

    if (!const_cast<VectorDB*>(this)->ensureConnection())
        return result;

    QSqlQuery q(m_db);
    q.exec("SELECT id, filename, filepath, total_chunks, added_date FROM documents ORDER BY added_date DESC");
    while (q.next()) {
        DocumentInfo info;
        info.id = q.value(0).toInt();
        info.filename = q.value(1).toString();
        info.filepath = q.value(2).toString();
        info.totalChunks = q.value(3).toInt();
        info.addedDate = q.value(4).toString();
        result.append(info);
    }
    return result;
}

// ─── Chunk Management ───

bool VectorDB::storeChunk(int documentId, int chunkIndex,
                           const QString &text, const QVector<float> &embedding,
                           const DocumentChunk::Metadata &metadata)
{
    QMutexLocker locker(&m_dbMutex);
    if (!ensureConnection()) return false;

    QSqlQuery q(m_db);
    q.prepare(
        "INSERT INTO chunks (document_id, chunk_index, text, embedding, "
        "page_number, section, subsection, heading, is_table, is_code_block, "
        "is_feature_list, is_summary, keywords, priority) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"
    );
    q.addBindValue(documentId);
    q.addBindValue(chunkIndex);
    q.addBindValue(text);

    QByteArray blob;
    blob.resize(embedding.size() * (int)sizeof(float));
    memcpy(blob.data(), embedding.constData(), blob.size());
    q.addBindValue(blob);

    q.addBindValue(metadata.pageNumber);
    q.addBindValue(metadata.section);
    q.addBindValue(metadata.subsection);
    q.addBindValue(metadata.heading);
    q.addBindValue(metadata.isTable ? 1 : 0);
    q.addBindValue(metadata.isCodeBlock ? 1 : 0);
    q.addBindValue(metadata.isFeatureList ? 1 : 0);
    q.addBindValue(metadata.isSummary ? 1 : 0);
    q.addBindValue(metadata.keywords.join(","));
    q.addBindValue(metadata.priority);

    if (!q.exec()) {
        emit dbError("Failed to store chunk: " + q.lastError().text());
        return false;
    }

    QSqlQuery update(m_db);
    update.prepare("UPDATE documents SET total_chunks = total_chunks + 1 WHERE id = ?");
    update.addBindValue(documentId);
    update.exec();

    return true;
}

QList<DocumentChunk> VectorDB::getChunks(int documentId) const
{
    QMutexLocker locker(&m_dbMutex);
    QList<DocumentChunk> result;

    if (!const_cast<VectorDB*>(this)->ensureConnection())
        return result;

    QSqlQuery q(m_db);
    q.prepare(
        "SELECT c.id, c.document_id, c.chunk_index, c.text, c.embedding, "
        "c.page_number, c.section, c.subsection, c.heading, c.is_table, "
        "c.is_code_block, c.is_feature_list, c.is_summary, c.keywords, c.priority, "
        "d.filename "
        "FROM chunks c JOIN documents d ON c.document_id = d.id "
        "WHERE c.document_id = ? ORDER BY c.chunk_index"
    );
    q.addBindValue(documentId);
    if (!q.exec()) return result;

    while (q.next()) {
        DocumentChunk chunk;
        chunk.id = q.value(0).toInt();
        chunk.documentId = q.value(1).toInt();
        chunk.chunkIndex = q.value(2).toInt();
        chunk.text = q.value(3).toString();

        QByteArray blob = q.value(4).toByteArray();
        if (blob.size() > 0) {
            chunk.embedding.resize(blob.size() / (int)sizeof(float));
            memcpy(chunk.embedding.data(), blob.constData(), blob.size());
        }

        chunk.metadata.pageNumber = q.value(5).toInt();
        chunk.metadata.section = q.value(6).toString();
        chunk.metadata.subsection = q.value(7).toString();
        chunk.metadata.heading = q.value(8).toString();
        chunk.metadata.isTable = q.value(9).toInt() != 0;
        chunk.metadata.isCodeBlock = q.value(10).toInt() != 0;
        chunk.metadata.isFeatureList = q.value(11).toInt() != 0;
        chunk.metadata.isSummary = q.value(12).toInt() != 0;
        chunk.metadata.keywords = q.value(13).toString().split(",", Qt::SkipEmptyParts);
        chunk.metadata.priority = q.value(14).toInt();
        chunk.filename = q.value(15).toString();

        result.append(chunk);
    }
    return result;
}

QList<DocumentChunk> VectorDB::getNeighborChunks(int documentId, int chunkIndex, int radius) const
{
    QList<DocumentChunk> chunks = getChunks(documentId);
    QList<DocumentChunk> result;

    for (const auto &chunk : chunks) {
        if (qAbs(chunk.chunkIndex - chunkIndex) <= radius)
            result.append(chunk);
    }
    return result;
}

// ─── Search Operations ───

QList<DocumentChunk> VectorDB::search(const QString &queryText,
                                       const QVector<float> &queryVector,
                                       int topK)
{
    m_stats.totalQueries.ref();

    QString cacheKey = getCacheKey(queryText, topK);
    QList<DocumentChunk> cached;
    if (getFromCache(cacheKey, cached)) {
        m_stats.cacheHits.ref();
        return cached;
    }
    m_stats.cacheMisses.ref();

    QList<DocumentChunk> results = fullScanSearch(queryVector, topK);
    if (results.isEmpty())
        results = approximateSearch(queryVector, topK);

    applyDiversityFiltering(results, topK);
    addToCache(cacheKey, results);

    return results;
}

QList<DocumentChunk> VectorDB::searchWithPriority(
    const QString &queryText,
    const QVector<float> &queryVector,
    int topK,
    const QStringList &prioritySections,
    const QStringList &negativeKeywords)
{
    if (!m_config.enableSectionPrioritization)
        return search(queryText, queryVector, topK);

    QList<DocumentChunk> results;

    for (const QString &section : prioritySections) {
        QList<DocumentChunk> sectionResults = searchBySection(queryText, queryVector, topK, section);
        double boost = m_config.prioritySectionBoost;
        for (auto &chunk : sectionResults)
            chunk.similarity *= boost;
        results.append(sectionResults);
        m_stats.prioritySectionHits.ref();
    }

    if (results.size() < topK) {
        QList<DocumentChunk> general = search(queryText, queryVector, topK - results.size());
        results.append(general);
    }

    if (m_config.enableValidation && !negativeKeywords.isEmpty())
        results = validateChunks(results, queryText, negativeKeywords);

    std::sort(results.begin(), results.end(), [](const DocumentChunk &a, const DocumentChunk &b) {
        return a.similarity > b.similarity;
    });

    if (results.size() > topK)
        results.erase(results.begin() + topK, results.end());

    return results;
}

QList<DocumentChunk> VectorDB::searchWithRerank(
    const QString &queryText,
    const QVector<float> &queryVector,
    int topK,
    const QStringList &prioritySections,
    const QStringList &negativeKeywords)
{
    if (!m_config.enableReranking)
        return searchWithPriority(queryText, queryVector, topK, prioritySections, negativeKeywords);

    QList<DocumentChunk> results = searchWithPriority(queryText, queryVector, topK * 2, prioritySections, negativeKeywords);
    results = rerankChunks(results, queryText, prioritySections);
    m_stats.rerankedChunks.ref();

    if (results.size() > topK)
        results.erase(results.begin() + topK, results.end());

    if (m_config.enableRetry && (results.isEmpty() || results.first().similarity < m_config.retryThreshold)) {
        m_stats.retryAttempts.ref();
        results = retrySearch(queryText, queryVector, topK, 1, prioritySections);
    }

    return results;
}

QFuture<QList<DocumentChunk>> VectorDB::searchAsync(
    const QString &queryText,
    const QVector<float> &queryVector,
    int topK)
{
    if (!m_config.enableAsyncSearch) {
        QList<DocumentChunk> result = search(queryText, queryVector, topK);
        QFutureInterface<QList<DocumentChunk>> fi;
        fi.reportFinished(&result);
        return fi.future();
    }

    QFuture<QList<DocumentChunk>> future = QtConcurrent::run([this, queryText, queryVector, topK]() {
        return search(queryText, queryVector, topK);
    });
    m_searchWatcher->setFuture(future);
    return future;
}

QString VectorDB::getContext(const QString &query,
                              const QVector<float> &queryVector,
                              int topK,
                              int maxTokens)
{
    QList<DocumentChunk> results = search(query, queryVector, topK);
    QString context;
    int tokens = 0;

    for (const auto &chunk : results) {
        int estimatedTokens = chunk.text.length() / 4;
        if (tokens + estimatedTokens > maxTokens)
            break;
        context += chunk.text + "\n\n";
        tokens += estimatedTokens;
    }
    return context.trimmed();
}

// ─── Search Helpers ───


QList<DocumentChunk> VectorDB::approximateSearch(const QVector<float> &queryVector, int limit)
{
    return fullScanSearch(queryVector, limit);
}

// ============================================================
// جایگزین تابع fullScanSearch در vector_db.cpp
// ============================================================

// بازنویسی با مدیریت امن اتصال دیتابیس در محیط Multi-thread
QList<DocumentChunk> VectorDB::fullScanSearch(const QVector<float> &queryVector, int limit) {
    QList<DocumentChunk> results;
    
    // در Qt، هر ترید باید اتصال دیتابیس خودش رو داشته باشه یا قفل گذاری خیلی دقیق بشه
    QMutexLocker locker(&m_dbMutex); 
    
    if (!m_db.isOpen()) return results;

    QSqlQuery q(m_db);
    q.prepare("SELECT c.id, c.document_id, c.chunk_index, c.text, c.embedding, d.filename "
              "FROM chunks c JOIN documents d ON c.document_id = d.id");
    
    if (!q.exec()) {
        emit dbError("Query failed: " + q.lastError().text());
        return results;
    }

    while (q.next()) {
        QByteArray blob = q.value(4).toByteArray();
        if (blob.isEmpty()) continue;

        // استخراج ایمن بردار از Blob
        int elementCount = blob.size() / sizeof(float);
        QVector<float> chunkVec(elementCount);
        memcpy(chunkVec.data(), blob.constData(), blob.size());

        if (chunkVec.size() != queryVector.size()) continue;

        double sim = computeCosineSimilarity(queryVector, chunkVec);
        
        if (sim >= m_config.minSimilarity) {
            DocumentChunk chunk;
            chunk.id = q.value(0).toInt();
            chunk.documentId = q.value(1).toInt();
            chunk.chunkIndex = q.value(2).toInt();
            chunk.text = q.value(3).toString();
            chunk.filename = q.value(5).toString();
            chunk.similarity = sim;
            results.append(chunk);
        }
    }

    // مرتب‌سازی بر اساس بیشترین شباهت
    std::sort(results.begin(), results.end(), [](const DocumentChunk &a, const DocumentChunk &b) {
        return a.similarity > b.similarity;
    });

    // اعمال محدودیت تعداد خروجی
    if (results.size() > limit) {
        results = results.mid(0, limit);
    }

    return results;
}

QList<DocumentChunk> VectorDB::searchBySection(
    const QString &queryText,
    const QVector<float> &queryVector,
    int topK,
    const QString &section)
{
    QList<DocumentChunk> results;
    if (!ensureConnection()) return results;

    QSqlQuery q(m_db);
    q.prepare("SELECT c.id, c.document_id, c.chunk_index, c.text, c.embedding, "
              "d.filename FROM chunks c JOIN documents d ON c.document_id = d.id "
              "WHERE c.section = ?");
    q.addBindValue(section);
    q.exec();

    struct ScoredChunk {
        DocumentChunk chunk;
        double score;
    };
    QList<ScoredChunk> scored;

    while (q.next()) {
        DocumentChunk chunk;
        chunk.id = q.value(0).toInt();
        chunk.documentId = q.value(1).toInt();
        chunk.chunkIndex = q.value(2).toInt();
        chunk.text = q.value(3).toString();
        chunk.filename = q.value(5).toString();

        QByteArray blob = q.value(4).toByteArray();
        if (blob.size() > 0) {
            chunk.embedding.resize(blob.size() / (int)sizeof(float));
            memcpy(chunk.embedding.data(), blob.constData(), blob.size());
            double sim = computeCosineSimilarity(queryVector, chunk.embedding);
            double keywordScore = computeKeywordScore(queryText, chunk.text);
            double combined = m_config.semanticWeight * sim + m_config.keywordWeight * keywordScore;
            chunk.similarity = combined;
            scored.append({chunk, combined});
        }
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredChunk &a, const ScoredChunk &b) {
        return a.score > b.score;
    });

    for (int i = 0; i < qMin(topK, scored.size()); ++i)
        results.append(scored[i].chunk);

    return results;
}

QList<DocumentChunk> VectorDB::validateChunks(
    const QList<DocumentChunk> &chunks,
    const QString &entity,
    const QStringList &negativeKeywords)
{
    QList<DocumentChunk> valid;
    for (const auto &chunk : chunks) {
        double confidence = calculateChunkConfidence(chunk, entity, negativeKeywords);
        if (confidence >= m_config.minConfidenceThreshold) {
            DocumentChunk c = chunk;
            c.similarity = confidence;
            valid.append(c);
        } else {
            m_stats.validationRejections.ref();
        }
    }
    return valid;
}

QList<DocumentChunk> VectorDB::rerankChunks(
    const QList<DocumentChunk> &chunks,
    const QString &query,
    const QStringList &prioritySections)
{
    QList<QPair<DocumentChunk, double>> scored;

    for (const auto &chunk : chunks) {
        double score = chunk.similarity;

        double keywordScore = computeKeywordScore(query, chunk.text);
        score += keywordScore * 0.3;

        if (!prioritySections.isEmpty() && chunk.isFromPrioritySection(prioritySections))
            score *= 1.2;

        if (chunk.metadata.isSummary || chunk.metadata.isFeatureList)
            score *= 1.1;

        if (chunk.metadata.isTable)
            score *= 0.9;

        scored.append({chunk, score});
    }

    std::sort(scored.begin(), scored.end(), [](const QPair<DocumentChunk, double> &a,
                                                const QPair<DocumentChunk, double> &b) {
        return a.second > b.second;
    });

    QList<DocumentChunk> result;
    for (const auto &pair : scored)
        result.append(pair.first);

    return result;
}

QList<DocumentChunk> VectorDB::retrySearch(
    const QString &queryText,
    const QVector<float> &queryVector,
    int topK,
    int attempt,
    const QStringList &prioritySections)
{
    if (attempt > m_config.maxRetrievalAttempts)
        return {};

    double relaxedThreshold = m_config.minSimilarity - (attempt * 0.05);
    int expandedTopK = topK * (attempt + 1);

    QList<DocumentChunk> results = fullScanSearch(queryVector, expandedTopK);

    if (!prioritySections.isEmpty()) {
        for (const QString &section : prioritySections) {
            QList<DocumentChunk> sectionResults = searchBySection(queryText, queryVector, topK, section);
            results.append(sectionResults);
        }
    }

    applyDiversityFiltering(results, topK);

    if (results.isEmpty() && attempt < m_config.maxRetrievalAttempts)
        return retrySearch(queryText, queryVector, topK, attempt + 1, prioritySections);

    return results;
}

double VectorDB::calculateChunkConfidence(
    const DocumentChunk &chunk,
    const QString &entity,
    const QStringList &negativeKeywords)
{
    double confidence = chunk.similarity;

    if (!negativeKeywords.isEmpty()) {
        for (const QString &kw : negativeKeywords) {
            if (chunk.text.contains(kw, Qt::CaseInsensitive))
                confidence *= 0.5;
        }
    }

    if (chunk.metadata.priority < 5)
        confidence *= 1.2;
    else if (chunk.metadata.priority > 15)
        confidence *= 0.8;

    confidence = qBound(0.0, confidence, 1.0);
    return confidence;
}

double VectorDB::computeKeywordScoreFast(const QSet<QString> &queryTokens, const QString &text) const
{
    if (queryTokens.isEmpty() || text.isEmpty())
        return 0.0;

    QString lowerText = text.toLower();
    int matches = 0;
    for (const auto &token : queryTokens) {
        if (lowerText.contains(token))
            ++matches;
    }
    return (double)matches / queryTokens.size();
}

double VectorDB::computeKeywordScore(const QString &query, const QString &chunk) const
{
    if (query.isEmpty() || chunk.isEmpty())
        return 0.0;

    QStringList queryWords = extractKeywords(query);
    QStringList chunkWords = extractKeywords(chunk);

    if (queryWords.isEmpty())
        return 0.0;

    int matches = 0;
    for (const auto &qw : queryWords) {
        for (const auto &cw : chunkWords) {
            if (cw.contains(qw, Qt::CaseInsensitive) || qw.contains(cw, Qt::CaseInsensitive)) {
                ++matches;
                break;
            }
        }
    }

    return (double)matches / queryWords.size();
}

QStringList VectorDB::extractKeywords(const QString &text) const
{
    QStringList words;
    QString current;
    for (const QChar &c : text) {
        if (c.isLetterOrNumber() || c == '_' || c == '-') {
            current += c;
        } else {
            if (current.length() >= 2)
                words.append(current.toLower());
            current.clear();
        }
    }
    if (current.length() >= 2)
        words.append(current.toLower());
    return words;
}

void VectorDB::expandNeighbors(
    const QList<DocumentChunk> &hits,
    QHash<QString, DocumentChunk> &mergedMap,
    int topK,
    int expansionRadius) const
{
    QSet<int> processedDocs;
    for (const auto &hit : hits)
        processedDocs.insert(hit.documentId);

    for (const auto &hit : hits) {
        QList<DocumentChunk> neighbors = const_cast<VectorDB*>(this)->getNeighborChunks(
            hit.documentId, hit.chunkIndex, expansionRadius);
        for (auto &n : neighbors) {
            QString key = n.cacheKey();
            if (!mergedMap.contains(key))
                mergedMap.insert(key, n);
        }
    }
}

void VectorDB::applyDiversityFiltering(QList<DocumentChunk> &results, int topK) const
{
    if (results.size() <= topK) return;

    QSet<int> seenDocuments;
    QList<DocumentChunk> diverse;
    QList<DocumentChunk> remaining;

    for (const auto &r : results) {
        if (!seenDocuments.contains(r.documentId) && diverse.size() < topK) {
            diverse.append(r);
            seenDocuments.insert(r.documentId);
        } else {
            remaining.append(r);
        }
    }

    for (const auto &r : remaining) {
        if (diverse.size() >= topK) break;
        diverse.append(r);
    }

    results = diverse;
}

// ─── Utility Functions ───

// بازنویسی بهینه شده با استفاده از محاسبات موازی ساده (SIMD-ready)
// فایل: vector_db.cpp
double VectorDB::computeCosineSimilarity(const QVector<float> &a, const QVector<float> &b) {
    // ۱. چک کردن ابعاد (بسیار حیاتی برای جلوگیری از کرش)
    int size = a.size();
    if (size == 0 || size != b.size()) {
        return 0.0;
    }

    // ۲. استفاده از اشاره‌گر مستقیم برای دسترسی فوق سریع به حافظه
    const float* ptrA = a.constData();
    const float* ptrB = b.constData();

    double dotProduct = 0.0;
    double normA = 0.0;
    double normB = 0.0;

    // ۳. حلقه محاسباتی بهینه (Compiler-Optimization Friendly)
    for (int i = 0; i < size; ++i) {
        float valA = ptrA[i];
        float valB = ptrB[i];
        
        dotProduct += static_cast<double>(valA) * valB;
        normA += static_cast<double>(valA) * valA;
        normB += static_cast<double>(valB) * valB;
    }

    // ۴. جلوگیری از تقسیم بر صفر (اگر برداری کاملاً صفر باشد)
    if (normA <= 1e-15 || normB <= 1e-15) {
        return 0.0;
    }

    // ۵. فرمول اصلی کسینوسی
    double similarity = dotProduct / (std::sqrt(normA) * std::sqrt(normB));

    // محدود کردن بین -1 و 1 برای جلوگیری از خطاهای شناور عددی
    return std::max(-1.0, std::min(1.0, similarity));
}

int VectorDB::totalChunkCount() const
{
    QMutexLocker locker(&m_dbMutex);
    if (!const_cast<VectorDB*>(this)->ensureConnection())
        return 0;

    QSqlQuery q(m_db);
    if (q.exec("SELECT COUNT(*) FROM chunks") && q.next())
        return q.value(0).toInt();
    return 0;
}

void VectorDB::clearAll()
{
    QMutexLocker locker(&m_dbMutex);
    if (!ensureConnection()) return;

    QSqlQuery q(m_db);
    q.exec("DELETE FROM chunks");
    q.exec("DELETE FROM documents");
    m_cache.clear();
}

// ─── Cache Management ───

QString VectorDB::getCacheKey(const QString &query, int topK) const
{
    return QString("%1_%2").arg(query).arg(topK);
}

bool VectorDB::getFromCache(const QString &key, QList<DocumentChunk> &results) const
{
    if (!m_config.enableCaching) return false;
    QMutexLocker locker(&m_cacheMutex);
    CacheEntry *entry = m_cache.object(key);
    if (entry) {
        results = entry->results;
        return true;
    }
    return false;
}

void VectorDB::addToCache(const QString &key, const QList<DocumentChunk> &results)
{
    if (!m_config.enableCaching) return;
    QMutexLocker locker(&m_cacheMutex);
    auto *entry = new CacheEntry();
    entry->results = results;
    entry->timestamp = QDateTime::currentMSecsSinceEpoch();
    m_cache.insert(key, entry);
}

// ─── Section Priority ───

void VectorDB::initSectionPriorityMap()
{
    m_config.sectionPriorityMap.clear();
    m_config.sectionPriorityMap["api"] = 1;
    m_config.sectionPriorityMap["reference"] = 2;
    m_config.sectionPriorityMap["documentation"] = 3;
    m_config.sectionPriorityMap["overview"] = 4;
    m_config.sectionPriorityMap["introduction"] = 5;
    m_config.sectionPriorityMap["syntax"] = 1;
    m_config.sectionPriorityMap["example"] = 2;
    m_config.sectionPriorityMap["parameters"] = 3;
    m_config.sectionPriorityMap["returns"] = 3;
    m_config.sectionPriorityMap["description"] = 5;
}

int VectorDB::getSectionPriority(const QString &section) const
{
    return m_config.sectionPriorityMap.value(section.toLower(), 10);
}

bool VectorDB::isHighPrioritySection(const QString &section) const
{
    return getSectionPriority(section) <= 3;
}
