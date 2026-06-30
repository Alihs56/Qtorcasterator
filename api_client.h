// ============================================================
// فایل: core/api_client.h
// ============================================================
#ifndef API_CLIENT_H
#define API_CLIENT_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QBuffer>
#include <QElapsedTimer>
#include <functional>

struct AIResponse {
    QString content;
    int sourcePort = 0;
    bool error = false;
    QString errorMessage;
};

class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject *parent = nullptr);

    // ===== توابع موجود در نسخه جدید (با ساختار مدرن) =====
    void sendChatRequest(int port, const QString &prompt, const QString &systemPrompt,
                         std::function<void(const AIResponse&)> callback);

    void sendVisionRequest(int port, const QString &prompt, const QImage &image,
                           std::function<void(const AIResponse&)> callback,
                           const QString &imagePath = {});

    // نسخه جدید: امبدینگ تک‌متن با خروجی QList<float>
    void sendEmbeddingRequest(int port, const QString &text,
                              std::function<void(const QList<float>&)> callback);

    void checkHealth(int port, std::function<void(bool, int)> callback);

    // ===== توابع اضافه شده از نسخه قدیمی (با حفظ ساختار جدید) =====
    // نسخه قدیمی: امبدینگ چندمتن با خروجی QList<QVector<float>>
    void sendEmbeddingRequest(int port, const QStringList &texts,
                              std::function<void(const QList<QVector<float>>&)> callback);

signals:
    void responseReceived(const AIResponse &response);
    void errorOccurred(const QString &error);

private:
    QNetworkAccessManager *m_manager;
};

#endif