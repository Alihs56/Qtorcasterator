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
                            QString("Verified: %1 — %2/%3 chunks stored (%.0f%%)").arg(res.filename).arg(stored).arg(total).arg(ratio * 100));
                    } else {
                        emit pdfVerified(res.filename, false,
                            QString("Warning: %1 — only %2/%3 chunks stored (%.0f%%)").arg(res.filename).arg(stored).arg(total).arg(ratio * 100));
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
    m_contextBuilder = new ContextBuilder(m_symbolDb, m_callGraph, m_depGraph, m_retrieverEngine, this);
    m_compressor = new ContextCompressor(this);
    m_verifier = new CodeVerifier(api, this);

    // Create Retriever with proper initialization
    auto embedFn = [this](const QString &text) -> QVector<float> {
        QVector<float> result;
        QEventLoop loop;
        QTimer timer;
        timer.setSingleShot(true);
        connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        
        m_embedder->getEmbedding(text, [&result, &loop](const QVector<float> &vec) {
            result = vec;
            loop.quit();
        });
        
        timer.start(5000);
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
}

VectorDB *Orchestrator::vectorDb() const { return m_db; }
EmbeddingClient *Orchestrator::embeddingClient() const { return m_embedder; }
PdfProcessor *Orchestrator::pdfProcessor() const { return m_pdfProc; }

bool Orchestrator::needRag(const QString &query)
{
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

void Orchestrator::processQuery(const QString &query, Mode mode, const QString &editorCode)
{
    if (m_processing.loadAcquire() != 0) {
        emit pipelineError("Already processing a request. Please wait.");
        return;
    }
    m_processing.storeRelease(1);
    emit processingStarted();
    LOG_INFO("Orchestrator", "Query processing started");
    emit processingFinished();
    m_processing.storeRelease(0);
}

void Orchestrator::processQueryWithImage(const QString &query, const QImage &image, Mode mode, const QString &imagePath)
{
    m_processing.storeRelease(0);
    emit processingFinished();
}

void Orchestrator::processQueryWithPdf(const QString &filepath, const QString &query, Mode mode, const QString &editorCode)
{
    m_processing.storeRelease(0);
    emit processingFinished();
}

void Orchestrator::processImage(const QImage &image)
{
    m_processing.storeRelease(0);
    emit processingFinished();
}

void Orchestrator::processPdf(const QString &filepath)
{
    m_processing.storeRelease(0);
    emit processingFinished();
}

void Orchestrator::processProgramQuery(const QString &query, const QString &code, Mode mode)
{
    m_processing.storeRelease(0);
    emit processingFinished();
}

void Orchestrator::processPdfQuery(const QString &query, Mode mode)
{
    m_processing.storeRelease(0);
    emit processingFinished();
}

void Orchestrator::applyPendingPatch()
{
    m_processing.storeRelease(0);
    emit processingFinished();
}

void Orchestrator::rejectPendingPatch()
{
    m_processing.storeRelease(0);
    emit processingFinished();
}
