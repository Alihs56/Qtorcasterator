#include "VectorStorageManager.h"
#include "logger.h"
#include <QFile>
#include <QDataStream>
#include <QDebug>
#include <algorithm>

#if FAISS_AVAILABLE
#include <faiss/IndexFlatL2.h>
#include <faiss/IndexIVFFlat.h>
#endif

VectorStorageManager::VectorStorageManager(const QString &indexPath, int dimension, QObject *parent)
    : QObject(parent), m_indexPath(indexPath), m_dimension(dimension)
{
    if (m_indexPath.isEmpty())
        m_indexPath = QDir::currentPath() + "/faiss_index.bin";
}

VectorStorageManager::~VectorStorageManager()
{
#if FAISS_AVAILABLE
    delete m_flatIndex;
    delete m_ivfIndex;
#endif
}

bool VectorStorageManager::initialize()
{
    QMutexLocker locker(&m_mutex);
    m_ready = true;
    return true;
}

bool VectorStorageManager::isReady() const
{
    return m_ready;
}

int VectorStorageManager::dimension() const
{
    return m_dimension;
}

int VectorStorageManager::generateId()
{
    return ++m_nextId;
}

bool VectorStorageManager::addVector(const VectorRecord &record)
{
    QMutexLocker locker(&m_mutex);
    VectorRecord rec = record;
    if (rec.id <= 0)
        rec.id = generateId();
    m_records.append(rec);
    emit vectorAdded(rec.id);
    return true;
}

bool VectorStorageManager::addVectors(const QList<VectorRecord> &records)
{
    for (const VectorRecord &rec : records)
        addVector(rec);
    return true;
}

bool VectorStorageManager::removeVector(int id)
{
    QMutexLocker locker(&m_mutex);
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records.at(i).id == id) {
            m_records.removeAt(i);
            emit vectorRemoved(id);
            return true;
        }
    }
    return false;
}

bool VectorStorageManager::removeVectors(const QList<int> &ids)
{
    for (int id : ids)
        removeVector(id);
    return true;
}

bool VectorStorageManager::updateVector(int id, const QVector<float> &newVector)
{
    QMutexLocker locker(&m_mutex);
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records.at(i).id == id) {
            m_records[i].vector = newVector;
            return true;
        }
    }
    return false;
}

bool VectorStorageManager::clear()
{
    QMutexLocker locker(&m_mutex);
    m_records.clear();
    m_nextId = 1;
    return true;
}

QList<VectorStorageManager::SearchResult> VectorStorageManager::searchTopK(const QVector<float> &query, int topK)
{
    QMutexLocker locker(&m_mutex);
    QList<SearchResult> results;

#if FAISS_AVAILABLE
    if (m_flatIndex && m_records.size() > 0) {
        int dim = m_dimension;
        if (query.size() != dim) {
            emit storageError(QString("Query dimension mismatch: %1 vs %2").arg(query.size()).arg(dim));
            return results;
        }

        int k = qMin(topK, static_cast<int>(m_records.size()));
        QVector<float> distances(k);
        QVector<long int> indices(k);

        try {
            m_flatIndex->search(1, query.constData(), k, distances.data(), indices.data());

            for (int i = 0; i < k; ++i) {
                if (indices[i] < 0 || indices[i] >= m_records.size()) continue;
                const VectorRecord &rec = m_records.at(indices[i]);
                SearchResult res;
                res.id = rec.id;
                res.distance = distances[i];
                res.similarity = 1.0f / (1.0f + distances[i]);
                res.metadata = rec.metadata;
                res.filePath = rec.filePath;
                res.chunkIndex = rec.chunkIndex;
                results.append(res);
            }
        } catch (const std::exception &e) {
            emit storageError(QString("FAISS search error: %1").arg(e.what()));
        }
        return results;
    }
#endif

    QList<QPair<float, int>> scored;
    float queryNorm = 0.0f;
    for (float v : query) queryNorm += v * v;
    queryNorm = std::sqrt(queryNorm);
    if (queryNorm == 0.0f) queryNorm = 1.0f;

    for (int i = 0; i < m_records.size(); ++i) {
        const VectorRecord &rec = m_records.at(i);
        if (rec.vector.size() != query.size()) continue;

        float dot = 0.0f;
        float vNorm = 0.0f;
        for (int j = 0; j < query.size(); ++j) {
            dot += query[j] * rec.vector[j];
            vNorm += rec.vector[j] * rec.vector[j];
        }
        vNorm = std::sqrt(vNorm);
        if (vNorm == 0.0f) vNorm = 1.0f;

        float cosine = dot / (queryNorm * vNorm);
        float l2 = 0.0f;
        for (int j = 0; j < query.size(); ++j) {
            float diff = query[j] - rec.vector[j];
            l2 += diff * diff;
        }

        SearchResult res;
        res.id = rec.id;
        res.distance = l2;
        res.similarity = cosine;
        res.metadata = rec.metadata;
        res.filePath = rec.filePath;
        res.chunkIndex = rec.chunkIndex;
        results.append(res);
    }

    std::sort(results.begin(), results.end(), [](const SearchResult &a, const SearchResult &b) {
        return a.similarity > b.similarity;
    });

    while (results.size() > topK)
        results.removeLast();

    return results;
}

QList<VectorStorageManager::SearchResult> VectorStorageManager::searchTopK(const QString &query,
                                                                          std::function<QVector<float>(const QString&)> embedFn,
                                                                          int topK)
{
    if (!embedFn) return {};
    QVector<float> vector = embedFn(query);
    return searchTopK(vector, topK);
}

bool VectorStorageManager::saveIndex()
{
    QMutexLocker locker(&m_mutex);
    QFile file(m_indexPath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit storageError("Cannot save index: " + file.errorString());
        return false;
    }

    QDataStream out(&file);
    out << m_dimension;
    out << m_nextId;
    out << static_cast<qint32>(m_records.size());
    for (const VectorRecord &rec : m_records) {
        out << rec.id;
        out << rec.documentId;
        out << rec.chunkIndex;
        out << rec.filePath;
        out << rec.metadata;
        out << rec.vector;
        out << rec.dimension;
    }
    file.close();
    emit indexSaved();
    return true;
}

bool VectorStorageManager::loadIndex()
{
    QMutexLocker locker(&m_mutex);
    QFile file(m_indexPath);
    if (!file.exists()) {
        emit indexLoaded();
        return true;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        emit storageError("Cannot load index: " + file.errorString());
        return false;
    }

    QDataStream in(&file);
    in >> m_dimension;
    in >> m_nextId;
    qint32 count;
    in >> count;
    m_records.clear();
    for (int i = 0; i < count; ++i) {
        VectorRecord rec;
        in >> rec.id;
        in >> rec.documentId;
        in >> rec.chunkIndex;
        in >> rec.filePath;
        in >> rec.metadata;
        in >> rec.vector;
        in >> rec.dimension;
        m_records.append(rec);
    }
    file.close();
    emit indexLoaded();
    return true;
}

int VectorStorageManager::totalVectors() const
{
    QMutexLocker locker(&m_mutex);
    return m_records.size();
}

bool VectorStorageManager::isTrained() const
{
    return m_trained;
}

void VectorStorageManager::ensureFaiss()
{
#if FAISS_AVAILABLE
    if (!m_flatIndex)
        m_flatIndex = new faiss::IndexFlatL2(m_dimension);
    if (!m_ivfIndex && !m_useIVF)
        m_ivfIndex = new faiss::IndexIVFFlat(m_flatIndex, m_dimension, 100);
#endif
    Q_UNUSED(m_trained);
}

// ============================================================
// جایگزین تابع rebuildFromRecords در VectorStorageManager.cpp
// ============================================================

void VectorStorageManager::rebuildFromRecords()
{
    QMutexLocker locker(&m_mutex);
    
#if FAISS_AVAILABLE
    try {
        ensureFaiss();
        if (m_flatIndex && m_records.size() > 0) {
            int n = m_records.size();
            int dim = m_dimension;
            QVector<float> data(n * dim);
            for (int i = 0; i < n; ++i) {
                const QVector<float> &vec = m_records.at(i).vector;
                for (int j = 0; j < qMin(dim, vec.size()); ++j)
                    data[i * dim + j] = vec[j];
            }
            
            m_flatIndex->reset();
            m_flatIndex->add(n, data.constData());
            m_trained = true;
            LOG_INFO("VectorStorage", QString("FAISS index rebuilt with %1 vectors").arg(n));
        }
    } catch (const faiss::FaissException &e) {
        QString errorMsg = QString("FAISS Exception: %1").arg(e.what());
        emit storageError(errorMsg);
        LOG_ERROR("VectorStorage", errorMsg);
        // ✅ Fallback: غیرفعال کردن FAISS و استفاده از جستجوی خطی
        m_useFAISS = false;
        m_trained = false;
        LOG_WARN("VectorStorage", "Falling back to linear search");
    } catch (const std::exception &e) {
        QString errorMsg = QString("Standard exception: %1").arg(e.what());
        emit storageError(errorMsg);
        LOG_ERROR("VectorStorage", errorMsg);
        m_useFAISS = false;
        m_trained = false;
    } catch (...) {
        QString errorMsg = "Unknown exception during FAISS rebuild";
        emit storageError(errorMsg);
        LOG_ERROR("VectorStorage", errorMsg);
        m_useFAISS = false;
        m_trained = false;
    }
#else
    LOG_WARN("VectorStorage", "FAISS not available, using linear search");
#endif
}