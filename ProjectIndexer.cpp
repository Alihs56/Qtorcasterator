#include "ProjectIndexer.h"
#include "CodeIndexer.h"
#include "LanguageDetector.h"
#include "CodeParser.h"
#include "SymbolDatabase.h"
#include "CallGraph.h"
#include "DependencyGraph.h"
#include "VectorStorageManager.h"
#include "Retriever.h"
#include "embedding_client.h"
#include "api_client.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QDebug>

ProjectIndexer::ProjectIndexer(LanguageDetector *detector, CodeParser *parser,
                               SymbolDatabase *symbolDb, CallGraph *callGraph,
                               DependencyGraph *depGraph, CodeIndexer *codeIndexer,
                               VectorStorageManager *vectorStore, Retriever *retriever,
                               EmbeddingClient *embedder, ApiClient *api,
                               QObject *parent)
    : QObject(parent),
      m_detector(detector),
      m_parser(parser),
      m_symbolDb(symbolDb),
      m_callGraph(callGraph),
      m_depGraph(depGraph),
      m_codeIndexer(codeIndexer),
      m_vectorStore(vectorStore),
      m_retriever(retriever),
      m_embedder(embedder),
      m_api(api)
{
    initialize();
    connectModules();
}

ProjectIndexer::~ProjectIndexer()
{
    stopIndexing();
}

void ProjectIndexer::initialize()
{
    QMutexLocker locker(&m_mutex);
    m_initialized = true;
}

void ProjectIndexer::connectModules()
{
    connect(m_codeIndexer, &CodeIndexer::indexingStarted, this, &ProjectIndexer::indexingStarted);
    connect(m_codeIndexer, &CodeIndexer::indexingFinished, this, &ProjectIndexer::indexingFinished);
    connect(m_codeIndexer, &CodeIndexer::indexingProgress, this, &ProjectIndexer::onCodeIndexerProgress);
    connect(m_codeIndexer, &CodeIndexer::fileIndexed, this, &ProjectIndexer::onCodeIndexerFileIndexed);
    connect(m_codeIndexer, &CodeIndexer::indexingError, this, &ProjectIndexer::indexingError);
}

void ProjectIndexer::setProjectDir(const QString &dir)
{
    QMutexLocker locker(&m_mutex);
    m_projectDir = dir;
    m_codeIndexer->setProjectDir(dir);
}

QString ProjectIndexer::projectDir() const
{
    QMutexLocker locker(&m_mutex);
    return m_projectDir;
}

void ProjectIndexer::startFullIndexing()
{
    QMutexLocker locker(&m_mutex);
    if (m_progress.isIndexing) return;

    m_progress.isIndexing = true;
    m_progress.processedFiles = 0;
    m_progress.processedChunks = 0;
    emit indexingProgress(m_progress);

    m_codeIndexer->startIndexing();
}

void ProjectIndexer::stopIndexing()
{
    m_codeIndexer->stopIndexing();
    QMutexLocker locker(&m_mutex);
    m_progress.isIndexing = false;
}

bool ProjectIndexer::isIndexing() const
{
    QMutexLocker locker(&m_mutex);
    return m_progress.isIndexing;
}

void ProjectIndexer::indexFile(const QString &filePath)
{
    m_codeIndexer->reindexFile(filePath);
}

void ProjectIndexer::removeFile(const QString &filePath)
{
    m_codeIndexer->removeFile(filePath);
    m_symbolDb->removeFile(filePath);
}

ProjectIndexer::IndexProgress ProjectIndexer::currentProgress() const
{
    QMutexLocker locker(&m_mutex);
    return m_progress;
}

int ProjectIndexer::totalIndexedFiles() const
{
    return m_codeIndexer->totalFilesIndexed();
}

int ProjectIndexer::totalIndexedSymbols() const
{
    return m_symbolDb->totalSymbols();
}

int ProjectIndexer::totalIndexedChunks() const
{
    return m_codeIndexer->totalChunks();
}

void ProjectIndexer::onCodeIndexerProgress(const QString &filePath, int current, int total)
{
    QMutexLocker locker(&m_mutex);
    m_progress.processedFiles = current;
    m_progress.totalFiles = total;
    m_progress.currentFile = filePath;
    emit indexingProgress(m_progress);
}

void ProjectIndexer::onCodeIndexerFinished()
{
    QMutexLocker locker(&m_mutex);
    m_progress.isIndexing = false;
    emit indexingFinished();
}

void ProjectIndexer::onCodeIndexerFileIndexed(const QString &filePath, int chunks, bool updated)
{
    QMutexLocker locker(&m_mutex);
    m_progress.processedChunks += chunks;
    emit fileIndexed(filePath, chunks, updated);
}
