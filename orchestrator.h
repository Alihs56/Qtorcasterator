#ifndef ORCHESTRATOR_H
#define ORCHESTRATOR_H

#include <QObject>
#include <QString>
#include <QImage>
#include <functional>
#include <QAtomicInt>

struct PatchModificationRequest {
    QString type;
    QString filePath;
    QString language;
    QString reason;
    QStringList oldCodeBlocks;
    QStringList newCodeBlocks;
    QList<int> startLines;
    QList<int> endLines;
    bool requiresConfirmation = true;
    QString rawPatchJson;
};

#include "planning_engine.h"
#include "language_intel.h"
#include "LanguageDetector.h"
#include "CodeParser.h"
#include "SymbolDatabase.h"
#include "CallGraph.h"
#include "DependencyGraph.h"
#include "CodeIndexer.h"
#include "VectorStorageManager.h"
#include "Retriever.h"
#include "Reranker.h"
#include "ContextBuilder.h"
#include "ContextCompressor.h"
#include "CodeVerifier.h"
#include "ProjectIndexer.h"
#include "CodeModificationEngine.h"
#include "GitBackupLayer.h"
#include "ProjectWorkspaceManager.h"
#include "FileContextResolver.h"
#include "ModificationController.h"
#include "GitBackupManager.h"

class ApiClient;
class VectorDB;
class EmbeddingClient;
class PdfProcessor;
class MemoryManager;
class RetrievalManager;
class ContextManager;
class PromptBuilder;
class ToolManager;
class ExecutionEngine;
class Reviewer;
class ProcessManager;

/**
 * @brief Orchestrator — coordinates the entire AI agent pipeline.
 *
 * Responsibilities:
 * - Control workflow
 * - Coordinate modules
 * - Manage state transitions
 *
 * It NEVER:
 * - Performs reasoning
 * - Retrieves documents
 * - Builds prompts
 * - Answers users
 */
class Orchestrator : public QObject
{
    Q_OBJECT
public:
    enum class Mode { Chat = 0, Plan = 1, Agent = 2 };

    explicit Orchestrator(ApiClient *api, ProcessManager *pm, QObject *parent = nullptr);

    void processQuery(const QString &query, Mode mode, const QString &editorCode = {});
    void processQueryWithImage(const QString &query, const QImage &image, Mode mode,
                               const QString &imagePath = {});
    void processQueryWithPdf(const QString &filepath, const QString &query, Mode mode,
                             const QString &editorCode = {});
    void processImage(const QImage &image);
    void processPdf(const QString &filepath);
    void processProgramQuery(const QString &query, const QString &code, Mode mode);
    void processPdfQuery(const QString &query, Mode mode);

    void setPlannerPort(int port);
    void setCoderPort(int port);
    void setExpertPort(int port);
    void setVisionPort(int port);
    void setEmbedPort(int port);
    void setProjectDir(const QString &dir);
    void setCurrentEditor(const QString &filePath, int line = 0, int column = 0)
    {
        m_memory->setCurrentEditor(filePath, line, column);
    }
    bool needRag(const QString &query);

    VectorDB *vectorDb() const;
    EmbeddingClient *embeddingClient() const;
    PdfProcessor *pdfProcessor() const;

public slots:
    void applyPendingPatch();
    void rejectPendingPatch();

    signals:
        void chatResponse(const QString &text);
        void planResponse(const QString &plan);
        void agentResponse(const QString &text);
        void pipelineLog(const QString &message);
        void pipelineError(const QString &error);
        void pipelineStage(const QString &stage, const QString &detail);
        void modelSwapRequested(int targetPort);
        void processingStarted();
        void processingFinished();
        void patchApprovalRequested(const QString &patchJson);
        void pdfProcessingStarted(const QString &filename);
        void pdfProcessingProgress(const QString &filename, int page, int totalPages);
        void pdfProcessed(const QString &filename, int chunks, int pages);
        void pdfError(const QString &filename, const QString &error);
        void pdfVerified(const QString &filename, bool success, const QString &message);

private:
    void classifyWithPlanner(const QString &query, const QString &ragContext);
    void handleClassification(const QString &response);
    void performRagSearch(const QString &query, std::function<void(const QString&)> callback);
    void executePlan(const QString &plan, const QString &ragContext, const QString &complexity,
                     int attempt, const QString &feedback, const QString &prevCode);
    void runReview(const QString &generatedCode, const QString &plan, int attempt);
    void sendToModel(int targetPort, const QString &fullPrompt, const QString &systemMsg,
                     std::function<void(const QString&)> onResult,
                     std::function<void()> onFail = nullptr);
    void waitForModelPort(int port, const QString &modelName, std::function<void()> onReady);
    void startVisionPipeline(const QString &query, const QImage &image, Mode mode,
                              const QString &imagePath);
    QString buildVisionPrompt(const QString &query, const PlanningEngine::PlanResult &plan) const;
    bool isModelReady(int port) const;

    void runAgentPipeline(const QString &query, const QString &code);
    void indexCodebase();
    QString buildArchitectureAnalysisPrompt(const QString &query, const QString &code) const;
    void buildAgentContext();
    void runAgentPlanner();
    void runAgentCoder();
    void runAgentReview(const QString &generatedCode);

    void runFullPipeline(const QString &query, const QString &code);
    void runCodeVerification(const QString &code, const QString &language);
    bool attemptCodeModification(const QString &generatedCode);

    PatchModificationRequest parsePatchJson(const QString &json);
    CodeModificationEngine::PatchInfo buildPatchInfo(const PatchModificationRequest &req);

    ApiClient *m_api;
    MemoryManager *m_memory;
    PlanningEngine *m_planner;
    RetrievalManager *m_retrieval;
    ContextManager *m_context;
    PromptBuilder *m_promptBuilder;
    ToolManager *m_tools;
    ExecutionEngine *m_execution;
    Reviewer *m_reviewer;
    ProcessManager *m_pm;

    VectorDB *m_db = nullptr;
    EmbeddingClient *m_embedder = nullptr;
    PdfProcessor *m_pdfProc = nullptr;

    LanguageIntelliService *m_langIntelli = nullptr;
    LanguageDetector *m_languageDetector = nullptr;
    CodeParser *m_codeParser = nullptr;
    SymbolDatabase *m_symbolDb = nullptr;
    CallGraph *m_callGraph = nullptr;
    DependencyGraph *m_depGraph = nullptr;
    CodeIndexer *m_codeIndexer = nullptr;
    VectorStorageManager *m_vectorStorage = nullptr;
    Retriever *m_retrieverEngine = nullptr;
    Reranker *m_reranker = nullptr;
    ContextBuilder *m_contextBuilder = nullptr;
    ContextCompressor *m_compressor = nullptr;
    CodeVerifier *m_verifier = nullptr;
    ProjectIndexer *m_projectIndexer = nullptr;
    CodeModificationEngine *m_codeModifier = nullptr;
    GitBackupLayer *m_gitBackup = nullptr;
    ProjectWorkspaceManager *m_workspaceManager = nullptr;
    FileContextResolver *m_contextResolver = nullptr;
    ModificationController *m_modController = nullptr;
    GitBackupManager *m_gitBackupManager = nullptr;

    int m_plannerPort = 8001;
    int m_coderPort = 8002;
    int m_expertPort = 8003;
    int m_visionPort = 8004;
    int m_embedPort = 8005;

    QString m_pendingQuery;
    QString m_conversationHistory;
    QString m_pendingRagContext;
    QString m_pendingPlan;
    QString m_pendingEditorCode;
    QString m_complexity;
    QImage m_pendingImage;
    QString m_pendingImagePath;
    bool m_pendingVision = false;
    Mode m_currentMode = Mode::Chat;
    QAtomicInt m_processing = 0;
    QString m_codebaseIndex;
    QString m_archAnalysis;
    QString m_agentContext;
    QString m_projectDir;
    QString m_pendingLanguage;
    QString m_pendingLanguageRules;
    PatchModificationRequest m_pendingPatch;
    bool m_awaitingApproval = false;
    int m_retries = 0;
};

#endif
