#include "embedding_client.h"
#include "api_client.h"
#include "logger.h"
#include <QTimer>

EmbeddingClient::EmbeddingClient(ApiClient *apiClient, QObject *parent)
    : QObject(parent), m_apiClient(apiClient), m_cache(CACHE_SIZE) {}

void EmbeddingClient::setPort(int port) {
    m_port = port;
}

int EmbeddingClient::port() const {
    return m_port;
}

QString EmbeddingClient::cacheKey(const QString &text) const {
    QByteArray hash = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Md5);
    return QString(hash.toHex());
}

// ===== پیاده‌سازی نسخه جدید با سیگنال‌ها =====
void EmbeddingClient::getEmbedding(const QString &text,
                                   std::function<void(const QVector<float>&)> callback) {
    QString key = cacheKey(text);

    {
        QMutexLocker locker(&m_cacheMutex);  // از نسخه قدیمی
        if (auto *cached = m_cache.object(key)) {
            if (callback) callback(*cached);
            return;
        }
    }

    m_apiClient->sendEmbeddingRequest(m_port, text,
        [this, text, key, callback](const QList<float> &vec) {
            if (vec.isEmpty()) {
                LOG_ERROR("EmbeddingClient", "Empty embedding returned for text: " + text.left(50));
                if (callback) callback({});
                emit embeddingError("Empty embedding returned");
                return;
            }

            QVector<float> result;
            for (float v : vec) result.append(v);

            {
                QMutexLocker locker(&m_cacheMutex);  // از نسخه قدیمی
                m_cache.insert(key, new QVector<float>(result));
            }
            
            emit embeddingReady(text, result);
            if (callback) callback(result);
        });
}

// ===== پیاده‌سازی اضافه شده از نسخه قدیمی: پردازش دسته‌ای با QTimer =====
// بازنویسی کامل برای ارسال درخواست‌های دسته‌ای و موازی
void EmbeddingClient::getEmbeddingBatch(const QStringList &texts, 
                                        std::function<void(const QList<QVector<float>>&)> callback) {
    if (texts.isEmpty()) { callback({}); return; }

    LOG_INFO("Embed", QString("Batch processing %1 chunks...").arg(texts.size()));

    // استفاده از متد Batch در ApiClient (اگر پیاده کردی) یا مدیریت موازی در اینجا
    // فرض بر این است که ApiClient نسخه لیست را پشتیبانی می‌کند
    m_apiClient->sendEmbeddingRequest(m_port, texts, [this, &texts, callback](const QList<QVector<float>> &results) {
        if (results.isEmpty()) {
            LOG_ERROR("Embed", "Batch embedding failed.");
            callback({});
            return;
        }

        // ذخیره در کش برای مراجعات بعدی
        QMutexLocker locker(&m_cacheMutex);
        for (int i = 0; i < results.size(); ++i) {
            if (i < results.size()) {
                m_cache.insert(cacheKey(texts[i]), new QVector<float>(results[i]));
            }
        }
        
        callback(results);
    });
}

void EmbeddingClient::clearCache() {
    QMutexLocker locker(&m_cacheMutex);
    m_cache.clear();
    LOG_INFO("EmbeddingClient", "Cache cleared");
}