#include "ProjectIndexer.h"
#include "CodeIndexer.h"
#include "logger.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMutexLocker>

ProjectIndexer::ProjectIndexer(LanguageDetector *detector, CodeParser *parser,
                               SymbolDatabase *symbolDb, CallGraph *callGraph,
                               DependencyGraph *depGraph, CodeIndexer *codeIndexer,
                               VectorStorageManager *vectorStore, Retriever *retriever,
                               EmbeddingClient *embedder, ApiClient *api, QObject *parent)
    : QObject(parent), m_detector(detector), m_parser(parser), m_symbolDb(symbolDb),
      m_callGraph(callGraph), m_depGraph(depGraph), m_codeIndexer(codeIndexer),
      m_vectorStore(vectorStore), m_retriever(retriever), m_embedder(embedder), m_api(api)
{
    initialize();
}

ProjectIndexer::~ProjectIndexer() {}

void ProjectIndexer::initialize()
{
    if (m_initialized) return;

    connectModules();
    m_initialized = true;
    LOG_INFO("ProjectIndexer", "Initialized");
}

void ProjectIndexer::connectModules()
{
    if (m_codeIndexer) {
        connect(m_codeIndexer, &CodeIndexer::progressUpdated, this,
                &ProjectIndexer::onCodeIndexerProgress);
        connect(m_codeIndexer, &CodeIndexer::finished, this,
                &ProjectIndexer::onCodeIndexerFinished);
    }
}

void ProjectIndexer::setProjectDir(const QString &dir)
{
    QMutexLocker locker(&m_mutex);
    m_projectDir = dir;
}

QString ProjectIndexer::projectDir() const
{
    QMutexLocker locker(&m_mutex);
    return m_projectDir;
}

void ProjectIndexer::startFullIndexing()
{
    QMutexLocker locker(&m_mutex);

    if (m_progress.isIndexing) {
        LOG_WARN("ProjectIndexer", "Indexing already in progress");
        return;
    }

    if (m_projectDir.isEmpty()) {
        emit indexingError("", "Project directory not set");
        return;
    }

    m_progress.isIndexing = true;
    m_progress.totalFiles = 0;
    m_progress.processedFiles = 0;
    m_progress.totalChunks = 0;
    m_progress.processedChunks = 0;

    emit indexingStarted();

    QDir dir(m_projectDir);
    QDirIterator it(dir, QDirIterator::Subdirectories);
    QStringList sourceFiles;

    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fi(filePath);
        if (fi.isFile()) {
            LanguageDetector::Language lang = m_detector ? m_detector->detect(filePath) : LanguageDetector::Unknown;
            if (lang != LanguageDetector::Unknown) {
                sourceFiles.append(filePath);
            }
        }
    }

    m_progress.totalFiles = sourceFiles.size();
    LOG_INFO("ProjectIndexer", QString("Starting indexing of %1 files").arg(sourceFiles.size()));

    for (const QString &file : sourceFiles) {
        indexFile(file);
    }
}

void ProjectIndexer::stopIndexing()
{
    QMutexLocker locker(&m_mutex);
    m_progress.isIndexing = false;
    if (m_codeIndexer) {
        // TODO: Add stop method to CodeIndexer
    }
}

bool ProjectIndexer::isIndexing() const
{
    QMutexLocker locker(&m_mutex);
    return m_progress.isIndexing;
}

void ProjectIndexer::indexFile(const QString &filePath)
{
    if (!m_codeIndexer) return;

    QFileInfo fi(filePath);
    if (!fi.exists()) {
        emit indexingError(filePath, "File not found");
        return;
    }

    LanguageDetector::Language lang = m_detector ? m_detector->detect(filePath) : LanguageDetector::Unknown;
    if (lang == LanguageDetector::Unknown) {
        LOG_WARN("ProjectIndexer", QString("Unknown language for %1").arg(filePath));
        return;
    }

    m_progress.currentFile = filePath;
    emit indexingProgress(m_progress);

    // Index the file
    m_codeIndexer->indexFile(filePath);
}

void ProjectIndexer::removeFile(const QString &filePath)
{
    if (m_symbolDb) {
        m_symbolDb->removeFile(filePath);
    }
    if (m_codeIndexer) {
        // TODO: Add removeFile method to CodeIndexer
    }
    LOG_INFO("ProjectIndexer", QString("Removed index for %1").arg(filePath));
}

ProjectIndexer::IndexProgress ProjectIndexer::currentProgress() const
{
    QMutexLocker locker(&m_mutex);
    return m_progress;
}

int ProjectIndexer::totalIndexedFiles() const
{
    if (m_symbolDb) {
        return m_symbolDb->totalFiles();
    }
    return 0;
}

int ProjectIndexer::totalIndexedSymbols() const
{
    if (m_symbolDb) {
        return m_symbolDb->totalSymbols();
    }
    return 0;
}

int ProjectIndexer::totalIndexedChunks() const
{
    QMutexLocker locker(&m_mutex);
    return m_progress.totalChunks;
}

void ProjectIndexer::onCodeIndexerProgress(const QString &filePath, int current, int total)
{
    QMutexLocker locker(&m_mutex);
    m_progress.currentFile = filePath;
    m_progress.processedChunks = current;
    m_progress.totalChunks = total;
    emit indexingProgress(m_progress);
}

void ProjectIndexer::onCodeIndexerFinished()
{
    QMutexLocker locker(&m_mutex);
    m_progress.processedFiles++;

    if (m_progress.processedFiles >= m_progress.totalFiles) {
        m_progress.isIndexing = false;
        emit indexingFinished();
        LOG_INFO("ProjectIndexer", "Indexing complete");
    }
}

void ProjectIndexer::onCodeIndexerFileIndexed(const QString &filePath, int chunks, bool updated)
{
    emit fileIndexed(filePath, chunks, updated);
}

void ProjectIndexer::processIndexedChunk(const CodeIndexer::CodeChunk &chunk)
{
    Q_UNUSED(chunk);
    // TODO: Process indexed chunks
}

void ProjectIndexer::updateVectorStore(const CodeIndexer::CodeChunk &chunk)
{
    Q_UNUSED(chunk);
    if (!m_vectorStore || !m_embedder) return;
    // TODO: Update vector store with embedding
}
