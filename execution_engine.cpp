#include "execution_engine.h"
#include "api_client.h"
#include "logger.h"
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QUrl>

ExecutionEngine::ExecutionEngine(ApiClient *api, ToolManager *tools, QObject *parent)
    : QObject(parent), m_api(api), m_tools(tools)
{
}

void ExecutionEngine::execute(const QString &plan, const QString &prompt, const QString &systemMsg,
                              int targetPort, std::function<void(const QString&, bool)> callback)
{
    Q_UNUSED(plan)

    m_api->sendChatRequest(targetPort, prompt, systemMsg,
        [callback](const AIResponse &res) {
            if (!res.error && callback) {
                callback(res.content, true);
            } else if (callback) {
                callback({}, false);
            }
        });
}

void ExecutionEngine::executeVision(const QString &query, const QImage &image, int targetPort,
                                    std::function<void(const QString&)> callback)
{
    m_api->sendVisionRequest(targetPort, query, image, [callback](const AIResponse &res) {
        if (!res.error && callback)
            callback(res.content);
        else if (callback)
            callback({});
    });
}
