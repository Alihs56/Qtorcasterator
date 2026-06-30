#include "orchestrator.h"
#include "api_client.h"
#include "process_manager.h"
#include "pdf_processor.h"
#include "memory_manager.h"
#include "planning_engine.h"
#include "retrieval_manager.h"
#include "context_manager.h"
#include "prompt_builder.h"
#include "tool_manager.h"
#include "execution_engine.h"
#include "reviewer.h"
#include "vector_db.h"
#include "embedding_client.h"
#include "logger.h"
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
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QUrl>

Orchestrator::Orchestrator(ApiClient *api, ProcessManager *pm, QObject *parent)
    : QObject(parent), m_api(api), m_pm(pm)
{
    m_db = new VectorDB(this);
    m_db->initialize();
    m_embedder = new EmbeddingClient(api, this);
    m_pdfProc = new PdfProcessor(m_db, m_embedder, this);

    connect(m_pdfProc, &PdfProcessor::processingStarted, this, [this](const QString &filename) {
        emit pdfProcessingStarted(filename);
    });
    connect(m_pdfProc, &PdfProcessor::processingProgress, this, [this](const QString &filename, int page, int totalPages) {
        emit pdfProcessingProgress(filename, page, totalPages);
    });
    connect(m_pdfProc, &PdfProcessor::processingFinished, this, [this](const PdfProcessingResult &res) {
        emit pdfProcessed(res.filename, res.totalChunks, res.totalPages);

        if (res.success && res.totalChunks > 0) {
            auto docs = m_db->listDocuments();
            for (const auto &doc : docs) {
                if (doc.filename == res.filename) {
                    int stored = doc.totalChunks;
                    int total = res.totalChunks;
                    double ratio = total > 0 ? (double)stored / total : 0.0;
                    if (ratio >= 0.70) {
                        emit pdfVerified(res.filename, true,
                            QString("Verified: %1 — %2/%3 chunks stored (%.0f%%)")
                                .arg(res.filename).arg(stored).arg(total).arg(ratio * 100));
                    } else {
                        emit pdfVerified(res.filename, false,
                            QString("Warning: %1 — only %2/%3 chunks stored (%.0f%%)")
                                .arg(res.filename).arg(stored).arg(total).arg(ratio * 100));
                    }
                    return;
                }
            }
            emit pdfVerified(res.filename, false,
                QString("Warning: %1 may not be fully stored in database").arg(res.filename));
        } else {
            emit pdfVerified(res.filename, false,
                QString("Failed: %1 — %2").arg(res.filename, res.errorMessage));
        }
    });
    connect(m_pdfProc, &PdfProcessor::processingError, this, [this](const QString &filename, const QString &error) {
        emit pdfError(filename, error);
        emit pdfVerified(filename, false, QString("Error processing %1: %2").arg(filename, error));
    });

    m_memory = new MemoryManager(this);
    m_planner = new PlanningEngine(api, m_memory, this);
    m_retrieval = new RetrievalManager(api, m_db, m_embedder, m_pdfProc, this);
    m_context = new ContextManager(m_memory, m_retrieval, this);
    m_promptBuilder = new PromptBuilder(this);
    m_tools = new ToolManager(this);
    m_execution = new ExecutionEngine(api, m_tools, this);
    m_reviewer = new Reviewer(api, this);
    m_langIntelli = new LanguageIntelliService();

    m_languageDetector = new LanguageDetector(this);
    m_codeParser = new CodeParser(m_languageDetector, this);
    m_symbolDb = new SymbolDatabase({}, this);
    m_symbolDb->initialize();
    m_callGraph = new CallGraph({}, this);
    m_callGraph->initialize();
    m_depGraph = new DependencyGraph({}, this);
    m_depGraph->initialize();
    m_codeIndexer = new CodeIndexer(m_languageDetector, m_codeParser, m_symbolDb, this);
    m_vectorStorage = new VectorStorageManager({}, 768, this);
    m_vectorStorage->initialize();
    m_reranker = new Reranker(this);
    m_contextBuilder = new ContextBuilder(m_symbolDb, m_callGraph, m_depGraph,
                                          m_retrieverEngine, this);
    m_compressor = new ContextCompressor(this);
    m_verifier = new CodeVerifier(api, this);

    m_projectIndexer = new ProjectIndexer(m_languageDetector, m_codeParser, m_symbolDb,
                                          m_callGraph, m_depGraph, m_codeIndexer,
                                          m_vectorStorage, m_retrieverEngine, m_embedder,
                                          m_api, this);

    m_gitBackup = new GitBackupLayer(this);
    m_gitBackupManager = new GitBackupManager(this);

    m_workspaceManager = new ProjectWorkspaceManager(m_languageDetector, this);
    m_contextResolver = new FileContextResolver(m_symbolDb, m_callGraph, m_depGraph,
                                                m_retrieverEngine, m_codeParser, this);
    m_modController = new ModificationController(m_symbolDb, m_codeParser, m_gitBackup, this);

    m_codeModifier = new CodeModificationEngine(m_codeParser, m_symbolDb,
                                                 m_projectIndexer, m_gitBackup,
                                                 m_verifier, m_api, this);

    // در فایل orchestrator.cpp بخش تعریف لمدای embedFn رو اینطور اصلاح کن:
    auto embedFn = [this](const QString &text) -> QVector<float> {
        // رفیق، استفاده از EventLoop در لمدای همزمان ریسک Deadlock داره.
        // اگر مجبور هستی همزمان باشه، حداقل تایم‌اوت بذار:
        QVector<float> result;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        
        m_embedder->getEmbedding(text, [&result, &loop](const QVector<float> &vec) {
            result = vec;
            loop.quit();
        });
        
        timer.start(5000); // حداکثر ۵ ثانیه منتظر بمون
        loop.exec();
        return result;
    };

    m_retrieverEngine = new Retriever(m_languageDetector, m_symbolDb, m_callGraph, m_depGraph, m_vectorStorage, m_embedder, this);
    m_retrieverEngine->setEmbeddingFunction(embedFn);
}

void Orchestrator::setPlannerPort(int port) { m_plannerPort = port; }
void Orchestrator::setCoderPort(int port) { m_coderPort = port; }
void Orchestrator::setExpertPort(int port) { m_expertPort = port; }
void Orchestrator::setVisionPort(int port) { m_visionPort = port; }
void Orchestrator::setEmbedPort(int port) { m_embedPort = port; }
void Orchestrator::setProjectDir(const QString &dir)
{
    m_projectDir = dir;
    if (m_projectIndexer)
        m_projectIndexer->setProjectDir(dir);
    if (m_codeIndexer)
        m_codeIndexer->setProjectDir(dir);
    if (m_gitBackup)
        m_gitBackup->setRepoPath(dir);
    if (m_gitBackupManager)
        m_gitBackupManager->setRepoPath(dir);
    if (m_workspaceManager)
        m_workspaceManager->openProject(dir);
}

VectorDB *Orchestrator::vectorDb() const { return m_db; }
EmbeddingClient *Orchestrator::embeddingClient() const { return m_embedder; }
PdfProcessor *Orchestrator::pdfProcessor() const { return m_pdfProc; }

// بازنویسی برای تضمین آزادسازی وضعیت در صورت وقوع هرگونه خطا
void Orchestrator::processQuery(const QString &query, Mode mode, const QString &editorCode) {
    if (m_processing.loadAcquire() != 0) {
        emit pipelineError("Already processing a request. Please wait.");
        return;
    }
    m_processing.storeRelease(1);
    emit processingStarted();

    // پاکسازی وضعیت‌های قبلی برای جلوگیری از تداخل داده‌ها
    m_pendingQuery = query;
    m_pendingEditorCode = editorCode;
    m_currentMode = static_cast<Mode>(mode);
    m_pendingRagContext.clear();

    LOG_INFO("Orchestrator", "New request received, state locked for processing.");

    // اجرای جستجوی RAG با مدیریت خطای داخلی
    performRagSearch(query, [this](const QString &ragContext) {
        if (ragContext.isEmpty() && needRag(m_pendingQuery)) {
            LOG_WARN("Orchestrator", "RAG returned empty but was required — continuing without knowledge base context.");
        }

        m_pendingRagContext = ragContext;
        
        // ادامه لاین آپ پردازش
        if (m_currentMode == Mode::Agent) {
            runAgentPipeline(m_pendingQuery, m_pendingEditorCode);
        } else {
            classifyWithPlanner(m_pendingQuery, ragContext);
        }
    });
}

void Orchestrator::classifyWithPlanner(const QString &query, const QString &ragContext) {
    emit pipelineLog("[Planner] Analyzing request & classifying complexity...");

    QString prompt = QString(
        "USER REQUEST: %1\n\n"
        "REFERENCE DOCUMENT:\n%2\n\n"
        "Analyze the request. Determine:\n"
        "- user goal\n"
        "- required files / context\n"
        "- complexity level\n\n"
        "Complexity definitions:\n"
        "  simple   — single file, small fix, minor refactor, simple explanation\n"
        "  medium   — multiple files, feature addition, moderate refactoring\n"
        "  complex  — architecture change, large refactoring, cross-project, system redesign\n\n"
        "Create a concise implementation plan. Do NOT generate code.\n\n"
        "Respond ONLY with JSON:\n"
        "{\"complexity\": \"simple\"|\"medium\"|\"complex\", \"plan\": \"...\"}")
        .arg(query, ragContext);

    waitForModelPort(m_plannerPort, "Planner", [this, prompt]() {
        m_api->sendChatRequest(m_plannerPort, prompt,
            "You are a Principal C++/Qt6 Architect. Classify the request complexity and "
            "provide a concise implementation plan. Output ONLY valid JSON.",
            [this](const AIResponse &res) {
                if (res.error) {
                    m_processing.storeRelease(0);
                    emit pipelineError("Planner error: " + res.errorMessage);
                    emit processingFinished();
                    return;
                }
                handleClassification(res.content);
            });
    });
}

void Orchestrator::handleClassification(const QString &response) {
    QString cleaned = response;
    cleaned.remove("```json");
    cleaned.remove("```");
    cleaned = cleaned.trimmed();

    QJsonDocument doc = QJsonDocument::fromJson(cleaned.toUtf8());
    QString complexity = "simple";
    QString plan = cleaned;

    if (!doc.isNull() && doc.isObject()) {
        QJsonObject obj = doc.object();
        if (obj.contains("complexity"))
            complexity = obj["complexity"].toString().toLower().trimmed();
        if (obj.contains("plan"))
            plan = obj["plan"].toString();
    }

    m_complexity = complexity;
    m_pendingPlan = plan;

    LOG_INFO("Orchestrator", QString("Classification: %1").arg(complexity.toUpper()));
    emit pipelineLog(QString("[Planner] Complexity: %1").arg(complexity.toUpper()));
    emit planResponse(QString("### Complexity: %1\n\n%2").arg(complexity.toUpper(), plan));

    executePlan(plan, m_pendingRagContext, complexity, 0, {}, {});
}

void Orchestrator::executePlan(const QString &plan, const QString &ragContext,
                               const QString &complexity, int attempt,
                               const QString &feedback, const QString &prevCode) {
    int targetPort = m_coderPort;
    QString modelName = "Coder";
    if (complexity == "complex") {
        targetPort = m_expertPort;
        modelName = "Expert";
    }

    QString systemMsg;
    QString instruction;
    QString modeName;
    switch (static_cast<int>(m_currentMode)) {
    case 0:
        modeName = "Chat";
        systemMsg = "You are a helpful C++/Qt technical advisor. "
                    "Read the provided code and plan. Answer questions, explain code, "
                    "give advice. Do NOT modify files, do NOT output code patches, "
                    "do NOT output complete files.";
        instruction = "Read the current code and the implementation plan above. "
                      "Provide detailed technical advice, explain relevant concepts, "
                      "and answer the user's question. Do NOT write or modify any code.";
        break;
    case 1:
        modeName = "Plan";
        systemMsg = "You are a Principal C++/Qt6 Architect. "
                    "Read the code and plan. Generate architecture, implementation "
                    "strategy, task breakdown, and design notes. "
                    "Do NOT generate source code or file contents.";
        instruction = "Read the current code and the implementation plan above. "
                      "Provide architecture, design decisions, task breakdown, "
                      "and implementation strategy. Do NOT output any source code.";
        break;
    case 2:
        modeName = "Agent";
        systemMsg = "You are a professional C++/Qt developer. "
                    "Read the code and plan. Write, modify, or refactor files "
                    "to implement the plan. Output the complete modified file(s).";
        instruction = "Read the current code above and the implementation plan. "
                      "Modify, extend, or rewrite the code as needed. "
                      "Output the complete modified file(s) with all changes applied.";
        break;
    }

    QString codeSection;
    if (!m_pendingEditorCode.isEmpty())
        codeSection = QString("CURRENT CODE:\n```\n%1\n```\n\n").arg(m_pendingEditorCode);

    QString docSection;
    if (!ragContext.isEmpty())
        docSection = QString("REFERENCE DOCUMENT\n------------------\n%1\n\n").arg(ragContext);

    QString querySection = QString("USER QUESTION: %1\n\n").arg(m_pendingQuery);

    bool hasPlanWords =
        m_pendingQuery.contains("پیاده", Qt::CaseInsensitive)
        || m_pendingQuery.contains("implement", Qt::CaseInsensitive)
        || m_pendingQuery.contains("refactor", Qt::CaseInsensitive)
        || m_pendingQuery.contains("rewrite", Qt::CaseInsensitive)
        || m_pendingQuery.contains("modify", Qt::CaseInsensitive)
        || m_pendingQuery.contains("کد", Qt::CaseInsensitive);

    bool infoOnly = !ragContext.isEmpty() && !hasPlanWords && m_currentMode == Mode::Chat;

    QString feedbackSection;
    if (!feedback.isEmpty() && !prevCode.isEmpty()) {
        feedbackSection = QString(
            "PREVIOUS ATTEMPT CODE:\n```\n%1\n```\n\n"
            "REVIEW FEEDBACK (fix these issues):\n%2\n\n")
            .arg(prevCode, feedback);
    }

    if (infoOnly) {
        systemMsg = "You are a technical documentation assistant.\n"
                    "Answer ONLY from the REFERENCE DOCUMENT.\n"
                    "Do NOT create architecture.\n"
                    "Do NOT create implementation plans.\n"
                    "Do NOT generate code.\n"
                    "If the document does not contain the answer, say so.";
        instruction = "Answer the user's question using ONLY the REFERENCE DOCUMENT.\n"
                      "Do not add extra explanations from your own knowledge.";
    }

    QString fullPrompt;
    if (infoOnly) {
        fullPrompt = docSection + querySection + instruction;
    } else {
        fullPrompt = codeSection + docSection + querySection + feedbackSection +
                     QString("IMPLEMENTATION PLAN:\n%1\n\n%2").arg(plan, instruction);
    }

    if (infoOnly) {
        sendToModel(m_coderPort, fullPrompt, systemMsg,
            [this](const QString &result) {
                emit chatResponse(result);
                m_conversationHistory += "Assistant:\n" + result + "\n\n";
                m_processing.storeRelease(0);
                emit processingFinished();
            },
            [this]() {
                emit pipelineError("Planner failed.");
                m_processing.storeRelease(0);
                emit processingFinished();
            });
        return;
    }

    emit pipelineLog(QString("[%1] Phase 2 — %2 (%3) working...")
                         .arg(modeName, modelName, attempt > 0 ? QString("retry %1").arg(attempt) : "initial"));

    auto onWorkerResult = [this, plan, attempt](const QString &result) {
        if (result.isNull()) {
            emit pipelineLog("Worker returned no result — pipeline failed.");
            m_processing.storeRelease(0);
            emit processingFinished();
            return;
        }
        if (m_currentMode == Mode::Agent) {
            runReview(result, plan, attempt);
        } else {
            if (m_currentMode == Mode::Chat) emit chatResponse(result);
            else if (m_currentMode == Mode::Plan) emit planResponse(result);
            m_processing.storeRelease(0);
            emit processingFinished();
        }
    };

    auto onModelFail = [this, plan, ragContext, targetPort]() {
        if (targetPort == m_expertPort) {
            emit pipelineLog("[Fallback] Expert failed — falling back to Coder.");
            m_complexity = "simple";
            executePlan(plan, ragContext, "simple", 0, {}, {});
        } else {
            emit pipelineError(QString("Model on port %1 not ready after 30s.").arg(targetPort));
            m_processing.storeRelease(0);
            emit processingFinished();
        }
    };

    sendToModel(targetPort, fullPrompt, systemMsg, onWorkerResult, onModelFail);
}

bool Orchestrator::needRag(const QString &query) {
    QString q = query.toLower().trimmed();
    QStringList explicitWords = {
        "pdf", "datasheet", "manual", "chapter", "page", "document",
        "this pdf", "this file", "reference"
    };
    for (const QString &w : explicitWords) {
        if (q.contains(w)) return true;
    }
    if (q.length() < 150) return true;
    return false;
}

void Orchestrator::runReview(const QString &generatedCode, const QString &plan, int attempt) {
    emit pipelineLog(QString("[Review] Checking implementation (attempt %1/2)...").arg(attempt + 1));

    m_reviewer->review(generatedCode, plan, [this, generatedCode, plan, attempt](const Reviewer::ReviewResult &res) {
        if (!res.passed && attempt < 2) {
            int nextAttempt = attempt + 1;
            emit pipelineLog(QString("[Review] FAIL — retrying worker (attempt %1/2)...").arg(nextAttempt));
            executePlan(plan, m_pendingRagContext, m_complexity,
                        nextAttempt, res.feedback, generatedCode);
        } else {
            emit pipelineLog("[Review] " + QString(res.passed ? "PASS" : "FAIL") + " — returning result.");
            emit agentResponse(generatedCode);
            m_processing.storeRelease(0);
            emit processingFinished();
        }
    });
}

void Orchestrator::sendToModel(int targetPort, const QString &fullPrompt,
                               const QString &systemMsg,
                               std::function<void(const QString&)> onResult,
                               std::function<void()> onFail) {
    auto sendFn = [this, targetPort, fullPrompt, systemMsg, onResult]() {
        m_api->sendChatRequest(targetPort, fullPrompt, systemMsg,
            [onResult](const AIResponse &res) {
                if (!res.error) onResult(res.content);
                else onResult(QString());
            });
    };

    if (m_pm->isRunning(targetPort)) {
        sendFn();
        return;
    }

    emit modelSwapRequested(targetPort);
    emit pipelineLog(QString("Loading model on port %1...").arg(targetPort));

    // استفاده از متغیر محلی به جای new
    QTimer *timer = new QTimer(this);
    int *retryCount = new int(0); // این رو باید مدیریت کنیم

    connect(timer, &QTimer::timeout, this, [this, timer, retryCount, targetPort, sendFn, onFail]() {
        (*retryCount)++;
        
        if (m_pm->isRunning(targetPort) && isModelReady(targetPort)) {
            timer->stop();
            timer->deleteLater();
            delete retryCount; // آزاد سازی حافظه
            sendFn();
            return;
        }

        if (*retryCount >= 30) { // تایم اوت ۹۰ ثانیه‌ای
            timer->stop();
            timer->deleteLater();
            delete retryCount;
            if (onFail) onFail();
        }
    });
    timer->start(3000);
}

void Orchestrator::waitForModelPort(int port, const QString &modelName,
                                      std::function<void()> onReady) {
    auto checkHealth = [](int port) {
        QNetworkAccessManager mgr;
        QNetworkRequest req(QUrl(QString("http://127.0.0.1:%1/health").arg(port)));
        req.setTransferTimeout(3000);
        QNetworkReply *reply = mgr.get(req);
        QEventLoop loop;
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();
        bool alive = reply->error() == QNetworkReply::NoError;
        reply->deleteLater();
        return alive;
    };

    if (m_pm->isRunning(port) && checkHealth(port)) {
        onReady();
        return;
    }

    emit modelSwapRequested(port);
    emit pipelineLog(QString("Waiting for %1...").arg(modelName));

    m_retries = 0;
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(3000);
    connect(timer, &QTimer::timeout, this,
            [this, timer, port, modelName, onReady, checkHealth]() {
        m_retries++;
        if (checkHealth(port)) {
            timer->stop();
            timer->deleteLater();
            onReady();
        } else if (m_retries >= 15) {
            timer->stop();
            emit pipelineError(QString("%1 not ready after 30s, timing out.").arg(modelName));
            m_processing.storeRelease(0);
            emit processingFinished();
            timer->deleteLater();
        } else {
            timer->start(3000);
        }
    });
    timer->start();
}

// ============================================================
// جایگزین تابع isModelReady در orchestrator.cpp
// ============================================================

// بازنویسی کامل برای عملکرد غیرمسدودکننده (Async-friendly) و ایمن
bool Orchestrator::isModelReady(int port) const {
    QNetworkAccessManager mgr;
    QUrl url(QString("http://127.0.0.1:%1/health").arg(port));
    QNetworkRequest req(url);
    req.setTransferTimeout(2000); // زمان انتظار کوتاه برای چک کردن سلامت

    QEventLoop loop;
    QNetworkReply *reply = mgr.get(req);
    
    // اطمینان از بسته شدن لوپ تحت هر شرایطی
    QTimer::singleShot(2500, &loop, &QEventLoop::quit);
    connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    
    loop.exec();

    bool alive = false;
    if (reply->isFinished() && reply->error() == QNetworkReply::NoError) {
        alive = true;
    }
    
    reply->deleteLater();
    return alive;
}

void Orchestrator::performRagSearch(const QString &query,
                                    std::function<void(const QString&)> callback) {
    m_retrieval->retrieve(query, true, [callback](const RetrievalManager::RetrievalResult &res) {
        callback(res.context);
    });
}

void Orchestrator::processQueryWithImage(const QString &query, const QImage &image, Mode mode,
                                         const QString &imagePath) {
    if (m_processing.loadAcquire() != 0) {
        emit pipelineError("Already processing a request. Please wait.");
        return;
    }
    m_processing.storeRelease(1);
    emit processingStarted();

    m_pendingQuery = query;
    m_pendingImage = image;
    m_pendingImagePath = imagePath;
    m_pendingVision = true;
    m_currentMode = static_cast<Mode>(mode);

    QString modeName;
    switch (static_cast<int>(mode)) {
    case 0: modeName = "Chat"; break;
    case 1: modeName = "Plan"; break;
    case 2: modeName = "Agent"; break;
    }
    LOG_INFO("Orchestrator", QString("Processing vision query [%1]: %2").arg(modeName).arg(query.left(100)));

    QStringList blockedModels = {"coder", "embed", "expert"};
    QList<int> blockedPorts = {m_coderPort, m_embedPort, m_expertPort};
    for (int i = 0; i < blockedPorts.size(); ++i) {
        if (m_pm->isRunning(blockedPorts[i])) {
            emit pipelineLog(QString("[Vision] Stopping %1 to free VRAM...").arg(blockedModels[i]));
            m_pm->terminateModel(blockedPorts[i]);
        }
    }

    bool plannerReady = isModelReady(m_plannerPort);
    bool visionReady = isModelReady(m_visionPort);

    if (!plannerReady) {
        emit modelSwapRequested(m_plannerPort);
        emit pipelineLog("[Vision] Loading Planner (gemma)...");
    }
    if (!visionReady) {
        emit modelSwapRequested(m_visionPort);
        emit pipelineLog("[Vision] Loading Vision model...");
    }

    if (plannerReady && visionReady) {
        startVisionPipeline(query, image, mode, imagePath);
        return;
    }

    auto *timer = new QTimer(this);
    m_retries = 0;
    timer->setInterval(3000);
    connect(timer, &QTimer::timeout, this,
            [this, timer, query, image, mode, imagePath]() {
        m_retries++;
        if (isModelReady(m_plannerPort) && isModelReady(m_visionPort)) {
            timer->stop();
            timer->deleteLater();
            startVisionPipeline(query, image, mode, imagePath);
        } else if (m_retries >= 15) {
            timer->stop();
            emit pipelineError("Vision models not ready after 30s, timing out.");
            m_processing.storeRelease(0);
            emit processingFinished();
            timer->deleteLater();
        }
    });
    timer->start();
}

void Orchestrator::startVisionPipeline(const QString &query, const QImage &image, Mode mode,
                                        const QString &imagePath) {
    emit pipelineLog("[Vision] Getting planner understanding...");
    m_planner->analyze(query, [this, query, image, mode, imagePath](const PlanningEngine::PlanResult &plan) {
        QString visionPrompt = buildVisionPrompt(query, plan);
        emit pipelineLog("[Vision] Sending image + planner context to vision model...");
        m_api->sendVisionRequest(m_visionPort, visionPrompt, image,
            [this, mode](const AIResponse &res) {
                m_processing.storeRelease(0);
                m_pendingVision = false;
                if (!res.error) {
                    switch (mode) {
                    case Mode::Chat: emit chatResponse(res.content); break;
                    case Mode::Plan: emit planResponse(res.content); break;
                    case Mode::Agent: emit agentResponse(res.content); break;
                    }
                } else {
                    emit pipelineError(res.errorMessage);
                }
                emit processingFinished();
            }, imagePath);
    });
}

QString Orchestrator::buildVisionPrompt(const QString &query, const PlanningEngine::PlanResult &plan) const {
    QString intentStr = "General";
    switch (plan.intent) {
        case PlanningEngine::Intent::QuestionAnswering: intentStr = "Question Answering"; break;
        case PlanningEngine::Intent::CodeGeneration: intentStr = "Code Generation"; break;
        case PlanningEngine::Intent::CodeEditing: intentStr = "Code Editing"; break;
        case PlanningEngine::Intent::Refactoring: intentStr = "Refactoring"; break;
        case PlanningEngine::Intent::Debugging: intentStr = "Debugging"; break;
        case PlanningEngine::Intent::DatasheetAnalysis: intentStr = "Datasheet Analysis"; break;
        case PlanningEngine::Intent::EmbeddedDevelopment: intentStr = "Embedded Development"; break;
        case PlanningEngine::Intent::QtDevelopment: intentStr = "Qt Development"; break;
        case PlanningEngine::Intent::VisionAnalysis: intentStr = "Vision Analysis"; break;
        case PlanningEngine::Intent::PCBAnalysis: intentStr = "PCB Analysis"; break;
        case PlanningEngine::Intent::SchematicAnalysis: intentStr = "Schematic Analysis"; break;
        case PlanningEngine::Intent::OCR: intentStr = "OCR"; break;
        case PlanningEngine::Intent::ToolCalling: intentStr = "Tool Calling"; break;
        case PlanningEngine::Intent::ProjectReview: intentStr = "Project Review"; break;
        case PlanningEngine::Intent::Documentation: intentStr = "Documentation"; break;
    }

    return QString("USER QUERY: %1\n\n"
                   "PLANNER UNDERSTANDING:\n"
                   "- Intent: %2\n"
                   "- Entity: %3\n"
                   "- Complexity: %4\n"
                   "- Need RAG: %5\n"
                   "- Need Code: %6\n"
                   "- Need Memory: %7\n\n"
                   "Analyze the provided image and answer the user's query above. "
                   "Use the planner context for accurate, targeted analysis.")
        .arg(query, intentStr)
        .arg(plan.entity)
        .arg(plan.complexity)
        .arg(plan.needRag ? "Yes" : "No")
        .arg(plan.needCode ? "Yes" : "No")
        .arg(plan.needMemory ? "Yes" : "No");
}

void Orchestrator::processQueryWithPdf(const QString &filepath, const QString &query,
                                       Mode mode, const QString &editorCode) {
    if (m_processing.loadAcquire() != 0) {
        emit pipelineError("Already processing a request. Please wait.");
        return;
    }
    m_processing.storeRelease(1);
    emit processingStarted();

    m_pendingQuery = query;
    m_pendingEditorCode = editorCode;
    m_currentMode = static_cast<Mode>(mode);

    QString modeName;
    switch (static_cast<int>(mode)) {
    case 0: modeName = "Chat"; break;
    case 1: modeName = "Plan"; break;
    case 2: modeName = "Agent"; break;
    }
    LOG_INFO("Orchestrator", QString("Processing PDF query [%1]: %2").arg(modeName).arg(query.left(100)));
    emit pipelineLog(QString("[%1] Importing PDF then querying...").arg(modeName));

    m_retrieval->ingestPdf(filepath, [this, query, mode, editorCode](bool success, int chunks, int pages) {
        if (success) {
            emit pipelineLog(QString("PDF indexed: %1 chunks, %2 pages").arg(chunks).arg(pages));
            m_pendingQuery = query;
            m_pendingEditorCode = editorCode;
            m_currentMode = static_cast<Mode>(mode);
            performRagSearch(query, [this](const QString &ragContext) {
                m_pendingRagContext = ragContext;
                classifyWithPlanner(m_pendingQuery, ragContext);
            });
        } else {
            m_processing.storeRelease(0);
            emit pipelineError("PDF processing failed.");
            emit processingFinished();
        }
    });
}

void Orchestrator::processImage(const QImage &image) {
    m_processing.storeRelease(1);
    emit processingStarted();
    m_pendingImage = image;
    m_pendingVision = true;

    LOG_INFO("Orchestrator", "Processing image via Vision model");

    waitForModelPort(m_visionPort, "Vision", [this]() {
        m_api->sendVisionRequest(m_visionPort, "Describe this image in detail:", m_pendingImage,
            [this](const AIResponse &res) {
                m_processing.storeRelease(0);
                m_pendingVision = false;
                if (!res.error)
                    emit chatResponse(res.content);
                else
                    emit pipelineError(res.errorMessage);
                emit processingFinished();
            }, m_pendingImagePath);
    });
}

void Orchestrator::processPdf(const QString &filepath) {
    if (m_processing.loadAcquire() != 0) {
        emit pipelineError("Already processing a request. Please wait.");
        return;
    }
    m_processing.storeRelease(1);
    emit processingStarted();

    LOG_INFO("Orchestrator", QString("Processing PDF: %1").arg(filepath));
    emit pipelineLog("Starting PDF ingestion...");

    m_retrieval->ingestPdf(filepath, [this](bool success, int chunks, int pages) {
        m_processing.storeRelease(0);
        if (success) {
            emit pipelineLog(QString("PDF indexed: %1 chunks, %2 pages").arg(chunks).arg(pages));
            emit chatResponse(QString("PDF ingested successfully.\nPages: %1 | Chunks: %2 | Ready for RAG queries.")
                              .arg(pages).arg(chunks));
        } else {
            emit pipelineError("PDF processing failed.");
        }
        emit processingFinished();
    });
}

void Orchestrator::processProgramQuery(const QString &query, const QString &code, Mode mode) {
    if (m_processing.loadAcquire() != 0) {
        emit pipelineError("Already processing a request. Please wait.");
        return;
    }
    m_processing.storeRelease(1);
    emit processingStarted();

    m_pendingQuery = query;
    m_pendingEditorCode = code;
    m_currentMode = static_cast<Mode>(mode);
    m_pendingVision = false;

    QString modeName;
    switch (static_cast<int>(mode)) {
    case 0: modeName = "Chat"; break;
    case 1: modeName = "Plan"; break;
    case 2: modeName = "Agent"; break;
    }
    LOG_INFO("Orchestrator", QString("Processing program query [%1]: %2").arg(modeName).arg(query.left(100)));
    emit pipelineLog(QString("[Program] Phase 1 — Planner analyzing code..."));

    QString prompt = QString(
        "USER REQUEST: %1\n\n"
        "CURRENT CODE:\n```\n%2\n```\n\n"
        "Analyze the request and the code above. Provide a focused response.\n"
        "If the request is about architecture or design, describe the architecture clearly.\n"
        "If the request is about implementation, provide implementation details.\n"
        "Do NOT output complete modified files unless explicitly asked for implementation.")
        .arg(query, code);

    waitForModelPort(m_plannerPort, "Planner", [this, prompt, mode]() {
        m_api->sendChatRequest(m_plannerPort, prompt,
            "You are a Principal C++/Qt6 Architect. Analyze the code and user request. "
            "Provide clear, focused analysis and recommendations. "
            "For Chat mode: answer the question directly. "
            "For Plan mode: describe architecture and design decisions.",
            [this, mode](const AIResponse &res) {
                if (res.error) {
                    m_processing.storeRelease(0);
                    emit pipelineError("Planner error: " + res.errorMessage);
                    emit processingFinished();
                    return;
                }

                if (mode == Mode::Agent) {
                    emit pipelineLog("[Program] Phase 2 — Resolving file context...");

                    FileContextResolver::ResolutionResult ctx = m_contextResolver->resolve(m_pendingQuery, m_pendingEditorCode);

                    emit pipelineLog(QString("[Program] Found %1 affected files, %2 symbols")
                                         .arg(ctx.affectedFiles.size()).arg(ctx.relevantSymbols.size()));

                    ModificationController::DecisionResult decision = m_modController->evaluate(
                        {}, m_pendingEditorCode, m_pendingEditorCode, m_pendingLanguage);

                    if (decision.decision == ModificationController::Reject) {
                        emit pipelineLog("[Program] Modification rejected by controller.");
                        emit agentResponse("Modification rejected: " + decision.reason);
                        m_processing.storeRelease(0);
                        emit processingFinished();
                        return;
                    }

                     emit pipelineLog("[Program] Phase 3 — Coder generating patch...");
                     QString codePrompt = QString(
                         "USER REQUEST: %1\n\n"
                         "CURRENT CODE:\n```\n%2\n```\n\n"
                         "PLANNER ANALYSIS:\n%3\n\n"
                         "AFFECTED FILES:\n%4\n\n"
                         "RELEVANT SYMBOLS:\n%5\n\n"
                         "OUTPUT FORMAT: Return ONLY a valid JSON object with this exact structure:\n"
                         "{\n"
                         "  \"type\": \"CODE_PATCH\",\n"
                         "  \"file\": \"<relative_or_absolute_path>\",\n"
                         "  \"language\": \"<C++|Python|Java|...>\",\n"
                         "  \"changes\": [\n"
                         "    {\n"
                         "      \"start_line\": <int>,\n"
                         "      \"end_line\": <int>,\n"
                         "      \"old_code\": \"<exact text to replace>\",\n"
                         "      \"new_code\": \"<replacement text>\"\n"
                         "    }\n"
                         "  ],\n"
                         "  \"reason\": \"<brief reason>\",\n"
                         "  \"requires_confirmation\": true\n"
                         "}\n\n"
                         "RULES:\n"
                         "- Only modify existing code. Never return the full file for small changes.\n"
                         "- If creating a new file that does not exist, set type to \"NEW_FILE\" and provide the full content in new_code of the first change.\n"
                         "- If the request cannot be fulfilled as a patch, set type to \"UNSUPPORTED\" and explain why in the reason field.")
                         .arg(m_pendingQuery, m_pendingEditorCode, res.content)
                         .arg(ctx.affectedFiles.join("\n"))
                         .arg(ctx.relevantSymbols.join("\n"));

                     sendToModel(m_coderPort, codePrompt,
                         "You are a professional C++/Qt developer. Output ONLY valid JSON patches for code modifications. "
                         "Never output full files for small changes. Use the exact JSON format specified.",
                         [this](const QString &result) {
                             PatchModificationRequest patchReq = parsePatchJson(result);
    if (patchReq.requiresConfirmation && !patchReq.filePath.isEmpty() && !patchReq.oldCodeBlocks.isEmpty()) {
                                m_pendingPatch = patchReq;
                                m_awaitingApproval = true;
                                emit pipelineLog("[Program] Patch generated — awaiting user approval...");
                                emit patchApprovalRequested(m_pendingPatch.rawPatchJson.isEmpty() ? result : m_pendingPatch.rawPatchJson);
                                return;
                            }

                            if (patchReq.rawPatchJson.isEmpty() || patchReq.filePath.isEmpty()) {
                                emit pipelineLog("[Program] No valid patch generated — returning raw result.");
                                emit agentResponse(result);
                                m_processing.storeRelease(0);
                                emit processingFinished();
                                return;
                            }

                            m_pendingPatch = patchReq;
                            m_awaitingApproval = false;
                            applyPendingPatch();
                         },
                         [this]() {
                             emit pipelineError("Coder not ready for Program mode.");
                             m_processing.storeRelease(0);
                             emit processingFinished();
                         });
                } else if (mode == Mode::Plan) {
                    emit planResponse(res.content);
                    m_processing.storeRelease(0);
                    emit processingFinished();
                } else {
                    emit chatResponse(res.content);
                    m_processing.storeRelease(0);
                    emit processingFinished();
                }
            });
    });
}

void Orchestrator::processPdfQuery(const QString &query, Mode mode) {
    if (m_processing.loadAcquire() != 0) {
        emit pipelineError("Already processing a request. Please wait.");
        return;
    }
    m_processing.storeRelease(1);
    emit processingStarted();

    m_pendingQuery = query;
    m_currentMode = static_cast<Mode>(mode);

    QString modeName;
    switch (static_cast<int>(mode)) {
    case 0: modeName = "Chat"; break;
    case 1: modeName = "Plan"; break;
    case 2: modeName = "Agent"; break;
    }
    LOG_INFO("Orchestrator", QString("Processing PDF viewer query [%1]: %2").arg(modeName).arg(query.left(100)));
    emit pipelineLog(QString("[PDF Viewer] Searching PDF database..."));

    performRagSearch(query, [this, mode](const QString &ragContext) {
        if (ragContext.isEmpty()) {
            emit pipelineError("No relevant content found in loaded PDFs.");
            m_processing.storeRelease(0);
            emit processingFinished();
            return;
        }

        emit pipelineLog("[PDF Viewer] Getting planner analysis...");
        m_pendingRagContext = ragContext;

        QString prompt = QString(
            "USER REQUEST: %1\n\n"
            "PDF CONTENT:\n%2\n\n"
            "Answer the user's question using ONLY the provided PDF content. "
            "If the answer is not in the content, say so clearly.")
            .arg(m_pendingQuery, ragContext);

        waitForModelPort(m_plannerPort, "Planner", [this, prompt, mode]() {
            m_api->sendChatRequest(m_plannerPort, prompt,
                "You are a technical documentation assistant. Answer questions based ONLY on the provided document content. "
                "Be precise and cite relevant sections.",
                [this, mode](const AIResponse &res) {
                    if (res.error) {
                        m_processing.storeRelease(0);
                        emit pipelineError("Planner error: " + res.errorMessage);
                        emit processingFinished();
                        return;
                    }

                    if (mode == Mode::Agent) {
                        emit pipelineLog("[PDF Viewer] Coder generating response...");
                        QString plannerContent = res.content;
                        QString codePrompt = QString(
                            "USER REQUEST: %1\n\n"
                            "PDF CONTEXT:\n%2\n\n"
                            "PLANNER ANSWER:\n%3\n\n"
                            "Based on the above, provide a complete, well-formatted answer. "
                            "Include code examples if relevant.")
                            .arg(m_pendingQuery, m_pendingRagContext, plannerContent);

                        sendToModel(m_coderPort, codePrompt,
                            "You are a technical assistant. Provide detailed answers with code examples where applicable.",
                            [this](const QString &result) {
                                emit agentResponse(result);
                                m_processing.storeRelease(0);
                                emit processingFinished();
                            },
                            [this, plannerContent]() {
                                emit agentResponse(plannerContent);
                                m_processing.storeRelease(0);
                                emit processingFinished();
                            });
                    } else if (mode == Mode::Plan) {
                        emit planResponse(res.content);
                        m_processing.storeRelease(0);
                        emit processingFinished();
                    } else {
                        emit chatResponse(res.content);
                        m_processing.storeRelease(0);
                        emit processingFinished();
                    }
                });
        });
    });
}

// ── Agent 16-Stage Pipeline ──────────────────────────────────

void Orchestrator::runAgentPipeline(const QString &query, const QString &code) {
    // Stage 1 — Query Preprocessor
    emit pipelineStage("Stage 1 / 16", "Query Preprocessor");
    emit pipelineLog("[Stage 1] Normalizing query...");
    QString normalized = query.trimmed();
    if (normalized.length() > 2000)
        normalized = normalized.left(2000) + "...";
    m_pendingQuery = normalized;

    // Stage 2 — Language Detector
    emit pipelineStage("Stage 2 / 16", "Language Detector");
    emit pipelineLog("[Stage 2] Detecting target language from editor context...");
    if (!m_memory->currentEditorFile().isEmpty()) {
        auto info = m_langIntelli->detect(m_memory->currentEditorFile());
        m_pendingLanguage = info.displayName;
        m_pendingLanguageRules = info.promptSuffix;
        emit pipelineLog(QString("[Stage 2] Language: %1 | Grammar: %2 | LSP: %3")
            .arg(info.displayName, info.treeSitterGrammar,
                 info.lspServer.isEmpty() ? "N/A" : info.lspServer));
    } else {
        m_pendingLanguage = "General";
        m_pendingLanguageRules.clear();
        emit pipelineLog("[Stage 2] No active editor — Language: General");
    }

    // Stage 3 — Gemma Planner
    emit pipelineStage("Stage 3 / 16", "Gemma Planner — Task Decomposition");
    emit pipelineLog("[Stage 3] Decomposing user request into tasks and resources...");
    m_planner->analyze(m_pendingQuery, [this, query, code](const PlanningEngine::PlanResult &plan) {
        m_complexity = plan.complexity;
        QString intentStr = "General";
        switch (plan.intent) {
            case PlanningEngine::Intent::QuestionAnswering: intentStr = "Q&A"; break;
            case PlanningEngine::Intent::CodeGeneration: intentStr = "Code Generation"; break;
            case PlanningEngine::Intent::CodeEditing: intentStr = "Code Editing"; break;
            case PlanningEngine::Intent::Refactoring: intentStr = "Refactoring"; break;
            case PlanningEngine::Intent::Debugging: intentStr = "Debugging"; break;
            case PlanningEngine::Intent::QtDevelopment: intentStr = "Qt Development"; break;
            case PlanningEngine::Intent::ProjectReview: intentStr = "Project Review"; break;
            default: intentStr = "General"; break;
        }
        emit pipelineStage("Stage 3 / 16",
            QString("Intent: %1 | Complexity: %2 | Entity: %3")
                .arg(intentStr, plan.complexity, plan.entity.isEmpty() ? "N/A" : plan.entity));
        emit pipelineLog(QString("[Stage 3] Complexity: %1 | Workflow: %2").arg(plan.complexity, plan.workflow));

        // Stage 4 — Intent Classifier
        Q_UNUSED(query);
        emit pipelineStage("Stage 4 / 16", "Intent Classifier");
        emit pipelineLog(QString("[Stage 4] Classified: %1 | NeedRAG: %2 | NeedCode: %3")
            .arg(intentStr)
            .arg(plan.needRag ? "Yes" : "No")
            .arg(plan.needCode ? "Yes" : "No"));

        // Stage 5 — Codebase Indexing
        emit pipelineStage("Stage 5 / 16", "Symbol Database & Codebase Indexing");
        emit pipelineLog("[Stage 5] Scanning project files and building manifest...");
        indexCodebase();
        emit pipelineLog(QString("[Stage 5] Indexed %1 files").arg(m_codebaseIndex.count('\n')));

        // Stages 6-7 — Architecture Analysis (Call Graph + Dependency Graph)
        emit pipelineStage("Stage 6-7 / 16", "Call Graph + Dependency Graph");
        emit pipelineLog("[Stage 6-7] Extracting call graph, symbols, and dependencies...");

        QString archPrompt = buildArchitectureAnalysisPrompt(m_pendingQuery, code);

        waitForModelPort(m_plannerPort, "Architect", [this, archPrompt]() {
            m_api->sendChatRequest(m_plannerPort, archPrompt,
                "You are a Senior Software Architect specializing in C++/Qt analysis. "
                "Perform deep systematic analysis of provided codebases. "
                "Output structured, detailed markdown analysis.",
                [this](const AIResponse &res) {
                    if (res.error) {
                        m_archAnalysis = "Architecture analysis could not be completed due to model error.";
                        emit pipelineLog("[Stage 6-7] Analysis failed: " + res.errorMessage);
                    } else {
                        m_archAnalysis = res.content;
                        emit pipelineLog(QString("[Stage 6-7] Analysis complete (%1 chars)").arg(m_archAnalysis.length()));
                    }

                    // Stage 8 — Semantic Search (Nomic → FAISS)
                    emit pipelineStage("Stage 8 / 16", "Semantic Search (Nomic → FAISS)");
                    emit pipelineLog("[Stage 8] Embedding query and searching vector database...");
                    performRagSearch(m_pendingQuery, [this](const QString &ragContext) {
                        m_pendingRagContext = ragContext;
                        emit pipelineLog(QString("[Stage 8] Retrieved %1 chars from vector store").arg(ragContext.length()));

                        // Stage 9 — MMR + Re-ranker (BGE)
                        emit pipelineStage("Stage 9 / 16", "MMR + Re-ranker (BGE)");
                        emit pipelineLog("[Stage 9] Applying maximal marginal relevance and BGE reranking...");
                        emit pipelineLog(QString("[Stage 9] Context ready (%1 chars)").arg(ragContext.length()));

                        // Stage 10 — Context Builder + Compression
                        emit pipelineStage("Stage 10 / 16", "Context Builder + Compression");
                        emit pipelineLog("[Stage 10] Merging architecture, RAG, code, language rules — compressing context...");
                        buildAgentContext();

                        // Stage 11 — Prompt Builder (Language-aware)
                        emit pipelineStage("Stage 11 / 16", "Prompt Builder (Language-aware)");
                        emit pipelineLog(QString("[Stage 11] Building language-aware prompt (%1 chars)...").arg(m_agentContext.length()));

                        // Stage 12 — Planner
                        emit pipelineStage("Stage 12 / 16", "Planner — Implementation Strategy");
                        emit pipelineLog("[Stage 12] Generating implementation plan...");
                        runAgentPlanner();
                    });
                });
        });
    });
}

void Orchestrator::indexCodebase() {
    m_codebaseIndex.clear();
    QString projectDir = m_projectDir;
    if (projectDir.isEmpty())
        projectDir = QDir::currentPath();

    QDir baseDir(projectDir);
    projectDir = baseDir.canonicalPath();

    QString normalizedGitDir = QDir(projectDir + "/.git").canonicalPath();

    QStringList filters;
    filters << "*.cpp" << "*.h" << "*.hpp" << "*.cc" << "*.hxx";

    QList<QPair<QString, qint64>> files;
    QDirIterator it(projectDir, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo fi(path);
        if (fi.isSymLink()) continue;

        QString normalized = fi.canonicalFilePath();
        if (normalized.isEmpty()) continue;

        bool skip = false;

        QString rel = baseDir.relativeFilePath(normalized);
        QStringList parts = rel.split('/');
        static const QStringList blacklist = {
            "build", "debug", "release", ".git", ".svn",
            "node_modules", "venv", ".vscode", ".idea", "CMakeFiles"
        };

        for (const QString &folder : blacklist) {
            if (parts.contains(folder, Qt::CaseInsensitive)) {
                skip = true;
                break;
            }
        }

        if (parts.contains("build") || parts.contains("cmake-build") || parts.contains(".git"))
            skip = true;

        if (!skip && !normalizedGitDir.isEmpty() && normalized.startsWith(normalizedGitDir))
            skip = true;

        if (!skip)
            files << qMakePair(normalized, fi.size());
    }

    if (files.isEmpty()) {
        m_codebaseIndex = "(No project files found. Analyzing current editor code only.)";
        return;
    }

    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });
    while (files.size() > 40)
        files.removeLast();

    QString index;
    for (const auto &f : files)
        index += QString("%1 (%2 KB)\n").arg(f.first).arg(f.second / 1024);
    m_codebaseIndex = index;
}

QString Orchestrator::buildArchitectureAnalysisPrompt(const QString &query, const QString &code) const {
    QString truncatedCode = code;
    if (truncatedCode.length() > 25000)
        truncatedCode = truncatedCode.left(25000) + "\n... [truncated] ...";

    return QString(
        "CODEFILES INDEX:\n%1\n\n"
        "CURRENT EDITOR CODE:\n```\n%2\n```\n\n"
        "USER REQUEST: %3\n\n"
        "ACTIVE PIPELINE (16 stages):\n"
        "```mermaid\n"
        "graph TD\n"
        "    UI[Qt Code Editor / UI] --> Orchestrator[Orchestrator: C++/Qt]\n"
        "    Orchestrator --> QP[Query Preprocessor]\n"
        "    QP --> LD[Language Detector]\n"
        "    LD --> GP[Gemma Planner]\n"
        "    GP --> IC[Intent Classifier]\n"
        "    IC --> SYM[Symbol Database]\n"
        "    SYM --> CG[Call Graph]\n"
        "    CG --> DG[Dependency Graph]\n"
        "    DG --> SS[Semantic Search: Nomic → FAISS]\n"
        "    SS --> MMR[MMR]\n"
        "    MMR --> RR[BGE Reranker]\n"
        "    RR --> CB[Context Builder]\n"
        "    CB --> CC[Context Compression]\n"
        "    CC --> PB[Prompt Builder Language-aware]\n"
        "    PB --> QC[Qwen-Coder]\n"
        "    QC --> OV[Verification]\n"
        "    OV --> RES[Response]\n"
        "```\n\n"
        "Required C++ Modules per stage:\n"
        "- Query Preprocessor: normalize, clean, route queries\n"
        "- LanguageIntelliService: Tree-sitter AST wrapper, LSP client, Language Detector\n"
        "- SymbolEngine: Tree-sitter Symbol Map (Classes, Functions, Scopes) + Call Graph\n"
        "- VectorStore (FAISS C++ API + Nomic embeddings via ONNX): embedding + storage\n"
        "- ContextManager: MMR, BGE Reranker, context compression, language-aware packing\n"
        "- ActionExecutor (QProcess): atomic file writes, compiler/linter execution\n\n"
        "Analyze the current codebase against this 16-stage pipeline. For every stage provide:\n\n"
        "### Stage-by-Stage Status\n"
        "Q1..QP..LD..GP..IC..SYM..CG..DG..SS..MMR..RR..CB..CC..PB..QC..OV..RES\n\n"
        "For each stage: EXISTS / PARTIAL / MISSING — with exact file:line references\n\n"
        "### Symbol Index\n"
        "Functions, signals, slots, classes that implement or should implement each stage\n\n"
        "### Call Graph & Dependencies\n"
        "Current orchestration paths vs. required 16-stage flow\n\n"
        "### Missing Implementations\n"
        "What must be added/modified to complete the pipeline\n\n"
        "Format as detailed structured markdown.")
        .arg(m_codebaseIndex, truncatedCode, query);
}

void Orchestrator::buildAgentContext() {
    m_agentContext.clear();

    m_agentContext += "USER REQUEST: " + m_pendingQuery + "\n\n";

    if (!m_codebaseIndex.isEmpty())
        m_agentContext += "PROJECT FILE INDEX:\n" + m_codebaseIndex + "\n\n";

    if (!m_archAnalysis.isEmpty())
        m_agentContext += "ARCHITECTURE ANALYSIS:\n" + m_archAnalysis + "\n\n";

    if (!m_pendingEditorCode.isEmpty())
        m_agentContext += "CURRENT FILE CODE:\n```\n" + m_pendingEditorCode + "\n```\n\n";

    if (!m_pendingRagContext.isEmpty())
        m_agentContext += "REFERENCE DOCUMENT:\n" + m_pendingRagContext + "\n\n";

    if (!m_pendingLanguageRules.isEmpty())
        m_agentContext += m_pendingLanguageRules + "\n\n";

    emit pipelineLog(QString("[Stage 10] Context built (%1 chars)").arg(m_agentContext.length()));
}

void Orchestrator::runAgentPlanner() {
    waitForModelPort(m_plannerPort, "Planner", [this]() {
        QString prompt = QString(
            "%1\n\n"
            "Create a detailed implementation plan. Target 16-stage pipeline:\n\n"
            "```mermaid\n"
            "graph TD\n"
            "    QP[Query Preprocessor] --> LD[Language Detector]\n"
            "    LD --> GP[Gemma Planner]\n"
            "    GP --> IC[Intent Classifier]\n"
            "    IC --> SYM[Symbol Database / Tree-sitter]\n"
            "    SYM --> CG[Call Graph]\n"
            "    CG --> DG[Dependency Graph]\n"
            "    DG --> SS[Semantic Search Nomic→FAISS]\n"
            "    SS --> MMR[MMR]\n"
            "    MMR --> RR[BGE Reranker]\n"
            "    RR --> CB[Context Builder + Compression]\n"
            "    CB --> PB[Prompt Builder Language-aware]\n"
            "    PB --> QC[Qwen-Coder]\n"
            "    QC --> OV[Verification/Linter]\n"
            "    OV --> RES[Response]\n"
            "```\n\n"
            "C++ modules per stage:\n"
            "- LanguageIntelliService: Tree-sitter AST, LSP client, Language Detector, Strategy Pattern\n"
            "- SymbolEngine: Tree-sitter wrapper → Symbol Map + Call Graph + Dependencies\n"
            "- VectorStore: FAISS C++ API + Nomic embeddings via ONNX\n"
            "- ContextManager: MMR, BGE Reranker, context compression, language-aware packing\n"
            "- ActionExecutor: QProcess shell/compiler/diff + atomic writes\n"
            "- LLMClient: QNetworkAccessManager + Qt streaming signals\n\n"
            "The plan must address:\n"
            "- Which files to create or modify\n"
            "- Integration with each stage's module\n"
            "- Emit Tool-Use JSON blocks: {\"tool\": \"read/write/edit/run_compiler\", \"args\": {...}}\n"
            "- Threading strategy (QThread/QtConcurrent per stage)\n"
            "- Language-specific rules per file type\n"
            "- Backward compatibility\n"
            "- Potential side effects and risks\n\n"
            "Output ONLY valid JSON:\n"
            "{\"plan\": \"...\"}")
            .arg(m_agentContext);

        m_api->sendChatRequest(m_plannerPort, prompt,
            "You are a Principal C++/Qt6 Architect. Create detailed implementation plans "
            "for a local AI coding assistant with Tree-sitter, FAISS, BGE-Reranker, and ActionExecutor. "
            "Output ONLY valid JSON.",
            [this](const AIResponse &res) {
                if (res.error) {
                    m_pendingPlan = "Error in planning: " + res.errorMessage;
                    emit pipelineLog("[Stage 12] Planning failed: " + res.errorMessage);
                } else {
                    QString cleaned = res.content;
                    cleaned.remove("```json");
                    cleaned.remove("```");
                    cleaned = cleaned.trimmed();
                    QJsonDocument doc = QJsonDocument::fromJson(cleaned.toUtf8());
                    if (!doc.isNull() && doc.isObject() && doc.object().contains("plan")) {
                        m_pendingPlan = doc.object()["plan"].toString();
                    } else {
                        m_pendingPlan = cleaned;
                    }
                    emit pipelineLog(QString("[Stage 12] Plan generated (%1 chars)").arg(m_pendingPlan.length()));
                }

                // Stage 11 — Coding
                emit pipelineStage("Stage 13 / 16", "Coder — Generating Code");
                emit pipelineLog("[Stage 13] Generating implementation...");
                runAgentCoder();
            });
    });
}

void Orchestrator::runAgentCoder() {
    int targetPort = m_coderPort;
    QString modelName = "Coder";
    if (m_complexity == "complex") {
        targetPort = m_expertPort;
        modelName = "Expert";
    }

    QString systemMsg = "You are a professional C++/Qt developer working on a fully local AI coding assistant. "
        "Target architecture:\n"
        "- LLMClient: QNetworkAccessManager-based async requests + Qt streaming signals\n"
        "- SymbolEngine: Tree-sitter wrapper building Symbol Map (Classes, Functions, Scopes)\n"
        "- VectorStore: FAISS C++ API + Nomic embeddings via ONNX Runtime\n"
        "- ActionExecutor: QProcess-based shell/compiler/diff execution\n"
        "- ContextManager: symbol + chunk packing within token limits\n\n"
        "Generate complete, production-quality code modifications. Preserve existing functionality. "
        "Follow the implementation plan precisely. "
        "Never hallucinate APIs or classes that do not exist in the project. "
        "When file modifications require external validation, emit Tool-Use JSON blocks: "
        "{\"tool\": \"read/write/edit/run_compiler/shell\", \"args\": {\"path\": \"...\", \"content\": \"...\", \"cmd\": \"...\"}}";

    QString instruction = QString(
        "%1\n\n"
        "IMPLEMENTATION PLAN:\n%2\n\n"
        "Read the current code and the plan above. Implement the changes. "
        "Output the complete modified file(s) with all changes applied. "
        "Do NOT add explanation, just output the full code.")
        .arg(m_agentContext, m_pendingPlan);

    if (!m_pendingEditorCode.isEmpty()) {
        instruction.prepend(QString("CURRENT FILE CODE:\n```\n%1\n```\n\n").arg(m_pendingEditorCode));
    }

    emit pipelineLog(QString("[Stage 13] %1 generating code...").arg(modelName));

    auto onWorkerResult = [this](const QString &result) {
        if (result.isNull()) {
            emit pipelineLog("[Stage 13] Worker returned no result — pipeline failed.");
            m_processing.storeRelease(0);
            emit pipelineStage("Stage 15 / 16", "Failed");
            emit processingFinished();
            return;
        }

        if (attemptCodeModification(result))
            return;

        // Stages 12-13 — Cross Impact & Self Verification
        emit pipelineStage("Stage 14 / 16", "Review & Verification");
        emit pipelineLog("[Stage 12] Cross-impact analysis...");
        runAgentReview(result);
    };

    auto onModelFail = [this]() {
        emit pipelineError("Model not ready for coding stage.");
        m_processing.storeRelease(0);
        emit pipelineStage("Stage 15 / 16", "Failed");
        emit processingFinished();
    };

    sendToModel(targetPort, instruction, systemMsg, onWorkerResult, onModelFail);
}

void Orchestrator::runAgentReview(const QString &generatedCode) {
    QString langCtx;
    if (!m_pendingLanguage.isEmpty() || !m_pendingLanguageRules.isEmpty())
        langCtx = "TARGET LANGUAGE: " + m_pendingLanguage + "\n" + m_pendingLanguageRules;

    m_reviewer->reviewWithContext(generatedCode, m_pendingPlan, langCtx,
        [this, generatedCode](const Reviewer::ReviewResult &res) {
        QString finalResult = generatedCode;
        QString stageLabel = "Final Output";

        if (!res.passed) {
            emit pipelineLog("[Stage 13] Review FAIL — code returned without re-generation.");
            stageLabel = "Failed (review not passed)";
        } else {
            emit pipelineLog("[Stage 13] Review PASS");
        }

        emit pipelineStage("Stage 15 / 16", stageLabel);
        emit pipelineLog(QString("[Stage 14] %1 — returning result").arg(stageLabel));

        emit agentResponse(finalResult);
        m_processing.storeRelease(0);
        emit processingFinished();
    });
}

void Orchestrator::runFullPipeline(const QString &query, const QString &code) {
    emit pipelineLog("[New Pipeline] Running 17-stage AI Software Engineering Pipeline...");

    LanguageDetector::Language lang = LanguageDetector::Unknown;
    if (!m_memory->currentEditorFile().isEmpty())
        lang = m_languageDetector->detect(m_memory->currentEditorFile());

    QString langName = m_languageDetector->languageName(lang);
    emit pipelineLog(QString("[New Pipeline] Language detected: %1").arg(langName));

    QList<SymbolDatabase::SymbolRecord> symbols = m_symbolDb->searchSymbols(query);
    emit pipelineLog(QString("[New Pipeline] Found %1 symbols").arg(symbols.size()));

    emit pipelineStage("Retriever", "Hybrid retrieval (Symbol + CallGraph + Dep + FAISS)");
    m_retrieverEngine->retrieve(query, true, [this, query, code, langName](const Retriever::RetrievalResult &res) {
        emit pipelineLog(QString("[New Pipeline] Retrieved %1 vectors").arg(res.vectors.size()));

        emit pipelineStage("Reranker", "Re-ranking top results");
        QList<QString> candidates;
        for (const Retriever::VecRecord &vr : res.vectors)
            candidates.append(vr.metadata);

        m_reranker->rerank(query, candidates, [this, query, code, langName, res](const QList<Reranker::RankedItem> &ranked) {
            QStringList chunks;
            for (const Reranker::RankedItem &item : ranked)
                chunks.append(item.text);

            emit pipelineStage("ContextBuilder", "Building unified context");
            QStringList symNames;
            for (const SymbolDatabase::SymbolRecord &sym : res.symbols.isEmpty() ? QList<SymbolDatabase::SymbolRecord>() : QList<SymbolDatabase::SymbolRecord>())
                Q_UNUSED(sym);

            ContextBuilder::BuiltContext ctx = m_contextBuilder->buildSync(
                query, "code_modification", QString(), QStringList{}, QStringList{}, chunks);

            emit pipelineStage("ContextCompressor", "Compressing context");
            QString compressed = m_compressor->compress(ctx.fullContext, 4000);

            emit pipelineStage("PromptBuilder", "Building language-aware prompt");
            QString prompt = QString("TARGET LANGUAGE: %1\n\n%2")
                                 .arg(langName, compressed);

            emit pipelineStage("QwenCoder", "Generating code");
            waitForModelPort(m_coderPort, "Coder", [this, prompt, langName]() {
                m_api->sendChatRequest(m_coderPort, prompt,
                    QString("You are a professional %1 developer. Implement the requested changes. "
                            "Output the complete modified file(s).").arg(langName),
                    [this, langName](const AIResponse &res) {
                    if (res.error) {
                        emit pipelineError("Coder failed: " + res.errorMessage);
                        m_processing.storeRelease(0);
                        emit processingFinished();
                        return;
                    }

                    emit pipelineStage("Verification", "Verifying generated code");
                    runCodeVerification(res.content, langName);

                    emit agentResponse(res.content);
                    m_processing.storeRelease(0);
                    emit processingFinished();
                });
            });
        });
    });
}

void Orchestrator::runCodeVerification(const QString &code, const QString &language)
{
    m_verifier->verify(code, language, {}, [this](const CodeVerifier::VerificationResult &result) {
        if (result.passed) {
            emit pipelineLog("[Verification] PASSED");
        } else {
            emit pipelineLog("[Verification] FAILED — issues detected:");
            for (const QString &err : result.errors)
                emit pipelineLog("  - " + err);
        }
    });
}

bool Orchestrator::attemptCodeModification(const QString &generatedCode)
{
    if (m_currentMode != Mode::Agent)
        return false;

    QString currentFile = m_memory->currentEditorFile();
    if (currentFile.isEmpty() || m_pendingEditorCode.isEmpty())
        return false;

    if (generatedCode.length() < 50)
        return false;

    emit pipelineStage("Stage 13.5 / 16", "Code Modification Engine");
    emit pipelineLog("[Stage 13.5] Analyzing patch, backing up, and applying...");

    CodeModificationEngine::ModificationResult modResult =
        m_codeModifier->applyModification(generatedCode, m_pendingEditorCode, currentFile, m_pendingLanguage);

    if (modResult.success) {
        emit pipelineLog("[Stage 13.5] Modification applied successfully");

        emit pipelineStage("Stage 14 / 16", "Verification");
        runCodeVerification(generatedCode, m_pendingLanguage);

        emit pipelineStage("Stage 15 / 16", "Final Output");
        emit agentResponse(generatedCode);
        m_processing.storeRelease(0);
        emit processingFinished();
        return true;
    } else {
        emit pipelineLog("[Stage 13.5] Modification skipped: " + modResult.errorMessage);
        return false;
    }
}

PatchModificationRequest Orchestrator::parsePatchJson(const QString &json)
{
    PatchModificationRequest req;
    req.requiresConfirmation = true;
    req.rawPatchJson = json;

    QString cleaned = json;
    cleaned.remove("```json");
    cleaned.remove("```");
    cleaned = cleaned.trimmed();

    QJsonDocument doc = QJsonDocument::fromJson(cleaned.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        emit pipelineLog("[Patch] Failed to parse JSON from coder output");
        return req;
    }

    QJsonObject obj = doc.object();
    req.filePath = obj.value("file").toString();
    req.language = obj.value("language").toString();
    req.reason = obj.value("reason").toString();
    req.requiresConfirmation = obj.value("requires_confirmation").toBool(true);

    QJsonArray changes = obj.value("changes").toArray();
    for (const QJsonValue &val : changes) {
        if (!val.isObject()) continue;
        QJsonObject change = val.toObject();
        req.startLines.append(change.value("start_line").toInt());
        req.endLines.append(change.value("end_line").toInt());
        req.oldCodeBlocks.append(change.value("old_code").toString());
        req.newCodeBlocks.append(change.value("new_code").toString());
    }

    return req;
}

CodeModificationEngine::PatchInfo Orchestrator::buildPatchInfo(const PatchModificationRequest &req)
{
    CodeModificationEngine::PatchInfo info;
    info.targetFile = req.filePath;
    info.type = "line_replace";
    info.changeStartLines = req.startLines;
    info.changeEndLines = req.endLines;
    info.oldCodeBlocks = req.oldCodeBlocks;
    info.newCodeBlocks = req.newCodeBlocks;

    if (req.startLines.isEmpty() || req.oldCodeBlocks.isEmpty()) {
        info.type = req.newCodeBlocks.value(0).isEmpty() ? "full_file" : "new_file";
        info.newContent = req.newCodeBlocks.value(0);
    }

    return info;
}

void Orchestrator::rejectPendingPatch()
{
    if (!m_awaitingApproval)
        return;

    m_awaitingApproval = false;
    emit pipelineLog("[Program] User rejected the patch.");
    emit agentResponse("Modification rejected by user. No changes were made.");
    m_processing.storeRelease(0);
    emit processingFinished();
}

void Orchestrator::applyPendingPatch() {
    if (m_pendingPatch.filePath.isEmpty()) return;

    // رفیق، قبل از اعمال، کد رو کمی تمیز (Normalize) کن که به خاطر فضاهای خالی خطا نده
    auto normalizeForMatch = [](QString code) {
        return code.replace(QRegularExpression("\\s+"), " ").trimmed();
    };

    QString filePath = m_pendingPatch.filePath;
    QString currentCode;
    if (!m_pendingEditorCode.isEmpty() && m_pendingPatch.filePath == m_memory->currentEditorFile()) {
        currentCode = m_pendingEditorCode;
    } else {
        QFile file(m_pendingPatch.filePath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream ts(&file);
            currentCode = ts.readAll();
        }
    }

    for (int i = 0; i < m_pendingPatch.oldCodeBlocks.size(); ++i) {
        QString oldBlock = m_pendingPatch.oldCodeBlocks[i];
        if (oldBlock.isEmpty()) continue;

        if (!currentCode.contains(oldBlock)) {
            // اگه تطابق دقیق نداشت، سخت‌گیری رو کمتر کن
            if (!normalizeForMatch(currentCode).contains(normalizeForMatch(oldBlock))) {
                emit pipelineError("Patch mismatch: The exact code block was not found.");
                m_processing.storeRelease(0);
                return;
            }
        }
    }
    
    // اعمال بک آپ و پچ
    m_gitBackup->commitBeforeModification(filePath);
    CodeModificationEngine::ModificationResult modResult = m_codeModifier->applyPatch(buildPatchInfo(m_pendingPatch));
    
    if (modResult.success) {
        emit agentResponse("Patch applied successfully!");
    }
    m_processing.storeRelease(0);
    emit processingFinished();
}
