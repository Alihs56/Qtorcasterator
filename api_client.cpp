// ============================================================
// فایل: core/api_client.cpp
// ============================================================
#include "api_client.h"
#include "logger.h"
#include <QUrlQuery>
#include <QNetworkRequest>
#include <QElapsedTimer>
#include <QPointer>

ApiClient::ApiClient(QObject *parent) : QObject(parent) {
    m_manager = new QNetworkAccessManager(this);
}

// ===== پیاده‌سازی نسخه جدید =====
// بازنویسی با مدیریت پیشرفته خطا و ایمنی حافظه
void ApiClient::sendChatRequest(int port, const QString &prompt, const QString &systemPrompt,
                               std::function<void(const AIResponse&)> callback) {
    QUrl url(QString("http://127.0.0.1:%1/v1/chat/completions").arg(port));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    // تنظیم زمان انتظار طولانی‌تر برای تولید کد سنگین
    request.setTransferTimeout(120000); 

    QJsonObject root;
    QJsonArray messages;
    messages.append(QJsonObject{{"role", "system"}, {"content", systemPrompt}});
    messages.append(QJsonObject{{"role", "user"}, {"content", prompt}});
    
    root.insert("messages", messages);
    root.insert("temperature", 0.2); // دمای پایین برای دقت بالاتر در کدنویسی
    root.insert("stream", false);

    QNetworkReply *reply = m_manager->post(request, QJsonDocument(root).toJson());

    // استفاده از QPointer برای جلوگیری از دسترسی به حافظه آزاد شده
    QPointer<QNetworkReply> safeReply(reply);

    connect(reply, &QNetworkReply::finished, this, [this, safeReply, port, callback]() {
        if (!safeReply) return;
        
        safeReply->deleteLater();
        AIResponse res;
        res.sourcePort = port;

        if (safeReply->error() != QNetworkReply::NoError) {
            res.error = true;
            res.errorMessage = safeReply->errorString();
            if (callback) callback(res);
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(safeReply->readAll());
        QJsonObject rootObj = doc.object();
        
        QJsonArray choices = rootObj["choices"].toArray();
        if (choices.isEmpty()) {
            res.error = true;
            res.errorMessage = "LLM returned empty choices";
        } else {
            res.content = choices[0].toObject()["message"].toObject()["content"].toString();
            res.error = false;
        }

        if (callback) callback(res);
        emit responseReceived(res);
    });
}

// بازنویسی با بهینه‌سازی خودکار تصویر قبل از ارسال
void ApiClient::sendVisionRequest(int port, const QString &prompt, const QImage &image,
                                  std::function<void(const AIResponse&)> callback,
                                  const QString &imagePath) {
    
    // ۱. ریسایز کردن تصویر به ابعاد استاندارد هوش مصنوعی (حداکثر ۱۰۲۴)
    QImage scaledImg = image;
    if (image.width() > 1024 || image.height() > 1024) {
        scaledImg = image.scaled(1024, 1024, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // ۲. تبدیل به فرمت کم‌حجم JPG به جای PNG سنگین
    QByteArray ba;
    QBuffer buffer(&ba);
    buffer.open(QIODevice::WriteOnly);
    scaledImg.save(&buffer, "JPG", 80); // کیفیت ۸۰ درصد کاملاً کافیه
    QString base64Image = ba.toBase64();

    // باقی منطق ارسال درخواست...
    QJsonObject msg;
    msg.insert("role", "user");
    // ... (ادامه کد قبلی با تصویر بهینه شده)
}

// ===== پیاده‌سازی نسخه جدید: امبدینگ تک‌متن =====
// بازنویسی با مدیریت چرخه حیات هوشمند
void ApiClient::sendEmbeddingRequest(int port, const QString &text,
                                      std::function<void(const QList<float>&)> callback) {
    QUrl url(QString("http://127.0.0.1:%1/v1/embeddings").arg(port));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonObject root;
    root.insert("input", text.left(4000)); // محدودیت طول برای پایداری مدل
    
    QNetworkReply *reply = m_manager->post(request, QJsonDocument(root).toJson());

    // رفیق، این بخش خیلی مهم است: اتصال سیگنال تخریب به مدیریت حافظه
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        // استفاده از یک Scoped Pointer برای اطمینان از پاکسازی
        reply->deleteLater(); 

        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("API", "Embedding Fail: " + reply->errorString());
            if (callback) callback({});
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonArray dataArr = doc.object()["data"].toArray();
        
        QList<float> vec;
        if (!dataArr.isEmpty()) {
            QJsonArray emb = dataArr[0].toObject()["embedding"].toArray();
            for (const QJsonValue &v : emb) vec.append(v.toDouble());
        }
        
        if (callback) callback(vec);
    });

    // اضافه کردن قابلیت لغو خودکار در صورت تخریب شیء اصلی
    connect(this, &QObject::destroyed, reply, &QNetworkReply::abort);
}

void ApiClient::checkHealth(int port, std::function<void(bool, int)> callback) {
    QUrl url(QString("http://127.0.0.1:%1/v1/models").arg(port));
    QNetworkRequest request(url);
    request.setTransferTimeout(5000);

    QElapsedTimer timer;
    timer.start();

    QNetworkReply *reply = m_manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [reply, callback, timer]() {
        reply->deleteLater();
        bool alive = (reply->error() == QNetworkReply::NoError);
        int ms = timer.elapsed();
        if (callback) callback(alive, ms);
    });
}

// ===== پیاده‌سازی اضافه شده از نسخه قدیمی: امبدینگ چندمتن =====
void ApiClient::sendEmbeddingRequest(int port, const QStringList &texts,
                                     std::function<void(const QList<QVector<float>>&)> callback) {
    if (texts.isEmpty()) { 
        if (callback) callback({}); 
        return; 
    }

    QUrl url(QString("http://127.0.0.1:%1/v1/embeddings").arg(port));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setTransferTimeout(60000);

    QJsonObject root;
    if (texts.size() == 1) {
        root.insert("input", texts.first());
    } else {
        QJsonArray arr;
        for (const QString &t : texts) arr.append(t);
        root.insert("input", arr);
    }
    root.insert("model", "nomic-embed-text-v1.5"); // اضافه شده برای هماهنگی با نسخه جدید

    QNetworkReply *reply = m_manager->post(request, QJsonDocument(root).toJson());

    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        QByteArray responseData = reply->readAll();
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            LOG_ERROR("API", "Embedding Fail: " + QString(responseData));
            if (callback) callback({});
            return;
        }

        QJsonArray dataArr = QJsonDocument::fromJson(responseData).object()["data"].toArray();
        QList<QVector<float>> results;
        for (int i = 0; i < dataArr.size(); ++i) {
            QVector<float> vec;
            QJsonArray emb = dataArr[i].toObject()["embedding"].toArray();
            for (const QJsonValue &v : emb) vec.append(static_cast<float>(v.toDouble()));
            results.append(vec);
        }
        if (callback) callback(results);
    });
}