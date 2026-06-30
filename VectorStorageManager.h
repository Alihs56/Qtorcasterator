#ifndef VECTORSTORAGEMANAGER_H
#define VECTORSTORAGEMANAGER_H

#include <QObject>
#include <QString>
#include <QList>
#include <QVector>
#include <QVector4D>
#include <QFile>
#include <QDir>
#include <QMutex>
#include <functional>
#include <cmath>

#ifndef FAISS_AVAILABLE
#define FAISS_AVAILABLE 0
#endif

#if FAISS_AVAILABLE
#include <faiss/IndexFlatL2.h>
#include <faiss/IndexIVFFlat.h>
#include <faiss/Index.h>
#endif

class VectorStorageManager : public QObject
{
    Q_OBJECT
public:
    struct VectorRecord {
        int id = -1;
        QString documentId;
        int chunkIndex = 0;
        QVector<float> vector;
        QString metadata;
        QString filePath;
        int dimension = 0;
    };

    struct SearchResult {
        int id = -1;
        float distance = 0.0f;
        float similarity = 0.0f;
        QString metadata;
        QString filePath;
        int chunkIndex = 0;
    };

    explicit VectorStorageManager(const QString &indexPath = QString(), int dimension = 768, QObject *parent = nullptr);
    ~VectorStorageManager() override;

    bool initialize();
    bool isReady() const;
    int dimension() const;

    bool addVector(const VectorRecord &record);
    bool addVectors(const QList<VectorRecord> &records);
    bool removeVector(int id);
    bool removeVectors(const QList<int> &ids);
    bool updateVector(int id, const QVector<float> &newVector);
    bool clear();

    QList<SearchResult> searchTopK(const QVector<float> &query, int topK);
    QList<SearchResult> searchTopK(const QString &query, std::function<QVector<float>(const QString&)> embedFn, int topK);

    bool saveIndex();
    bool loadIndex();

    int totalVectors() const;
    bool isTrained() const;

signals:
    void vectorAdded(int id);
    void vectorRemoved(int id);
    void indexSaved();
    void indexLoaded();
    void storageError(const QString &error);

private:
    void ensureFaiss();
    void rebuildFromRecords();
    int generateId();

    QString m_indexPath;
    int m_dimension = 768;
    bool m_ready = false;
    bool m_trained = false;
    int m_nextId = 1;

    QList<VectorRecord> m_records;
    QList<SearchResult> m_searchResults;

    mutable QMutex m_mutex;

#if FAISS_AVAILABLE
    faiss::IndexFlatL2 *m_flatIndex = nullptr;
    faiss::IndexIVFFlat *m_ivfIndex = nullptr;
    bool m_useIVF = false;
#endif
};

#endif // VECTORSTORAGEMANAGER_H
