#ifndef PLANNING_ENGINE_H
#define PLANNING_ENGINE_H

#include <QObject>
#include <QString>
#include <functional>
#include "memory_manager.h"

class ApiClient;

/**
 * @brief Planning Engine — determines user intent, entity, and workflow.
 *
 * Responsibilities:
 * - Analyze user query to determine intent
 * - Identify current entity (e.g., PIC18F452)
 * - Classify task complexity
 * - Determine what resources are needed (RAG, Vision, Code, Tools, Memory)
 * - Never retrieves documents, never writes code, never answers the user
 */
class PlanningEngine : public QObject
{
    Q_OBJECT
public:
    enum class Intent {
        QuestionAnswering,
        CodeGeneration,
        CodeEditing,
        Refactoring,
        Debugging,
        DatasheetAnalysis,
        EmbeddedDevelopment,
        QtDevelopment,
        VisionAnalysis,
        PCBAnalysis,
        SchematicAnalysis,
        OCR,
        ToolCalling,
        ProjectReview,
        Documentation
    };

    struct PlanResult {
        Intent intent = Intent::QuestionAnswering;
        QString entity;
        QString complexity; // "simple", "medium", "complex"
        bool needRag = false;
        bool needVision = false;
        bool needCode = false;
        bool needTool = false;
        bool needMemory = false;
        QString expectedOutput;
        QString workflow;
    };

    explicit PlanningEngine(ApiClient *api, MemoryManager *memory, QObject *parent = nullptr);

    void analyze(const QString &query, std::function<void(const PlanResult&)> callback);

    bool needRag(const QString &query) const;

signals:
    void planReady(const PlanResult &plan);
    void planError(const QString &error);

private:
    ApiClient *m_api;
    MemoryManager *m_memory;
};

#endif
