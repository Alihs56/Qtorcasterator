#ifndef EMBEDDING_CLIENT_H
#define EMBEDDING_CLIENT_H

#include <QObject>
#include <QVector>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>
#include <QCache>
#include <QCryptographicHash>
#include <QMutex>

class ApiClient;

class EmbeddingClient : public QObject {
    Q_OBJECT
public:
    explicit EmbeddingClient(ApiClient *apiClient, QObject *parent = nullptr);

    // ===== توابع اصلی (نسخه جدید) =====
    void getEmbedding(const QString &text,
                      std::function<void(const QVector<float>&)> callback);
    
    // ===== تابع اضافه شده از نسخه قدیمی: پردازش دسته‌ای با مدیریت همزمانی =====
    void getEmbeddingBatch(const QStringList &texts,
                           std::function<void(const QList<QVector<float>>&)> callback);

    void setPort(int port);
    int port() const;

    void clearCache();

signals:
    void embeddingReady(const QString &text, const QVector<float> &vector);
    void embeddingError(const QString &error);

private:
    QString cacheKey(const QString &text) const;

    ApiClient *m_apiClient;
    int m_port = 8005;
    QCache<QString, QVector<float>> m_cache;
    QMutex m_cacheMutex;  // از نسخه قدیمی برای امنیت نخ
    static constexpr int CACHE_SIZE = 500;
};

#endif