#ifndef EXECUTION_ENGINE_H
#define EXECUTION_ENGINE_H

#include <QObject>
#include <QString>
#include <QImage>
#include <functional>

class ApiClient;
class ToolManager;
class PromptBuilder;
struct AIResponse;

/**
 * @brief Execution Engine — executes workflows based on plan.
 *
 * Responsibilities:
 * - Send prompts to appropriate model
 * - Handle model loading/unloading
 * - Stream output
 * - Return final result
 * - Never plans, never retrieves, never builds prompts
 */
class ExecutionEngine : public QObject
{
    Q_OBJECT
public:
    explicit ExecutionEngine(ApiClient *api, ToolManager *tools, QObject *parent = nullptr);

    void execute(const QString &plan, const QString &prompt, const QString &systemMsg,
                 int targetPort, std::function<void(const QString&, bool needsReview)> callback);

    void executeVision(const QString &query, const QImage &image, int targetPort,
                       std::function<void(const QString&)> callback);

signals:
    void executionComplete(const QString &result);
    void executionError(const QString &error);
    void modelSwapRequested(int targetPort);

private:
    ApiClient *m_api;
    ToolManager *m_tools;
};

#endif
