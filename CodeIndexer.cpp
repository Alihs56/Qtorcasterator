#include "CodeIndexer.h"
#include "LanguageDetector.h"
#include "CodeParser.h"
#include "SymbolDatabase.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTextStream>
#include <QDebug>

CodeIndexer::CodeIndexer(LanguageDetector *detector, CodeParser *parser,
                         SymbolDatabase *symbolDb, QObject *parent)
    : QObject(parent), m_detector(detector), m_parser(parser), m_symbolDb(symbolDb)
{
    m_watcher = new QFileSystemWatcher(this);
    m_debounceTimer = new QTimer(this);
    m_debounceTimer->setSingleShot(true);
    m_debounceTimer->setInterval(500);

    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &CodeIndexer::onFileChanged);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &CodeIndexer::onDirChanged);
    connect(m_debounceTimer, &QTimer::timeout, this, [this]() {
        QSet<QString> toUpdate;
        {
            QMutexLocker locker(&m_mutex);
            toUpdate = m_changedFiles;
            m_changedFiles.clear();
        }
        for (const QString &path : toUpdate) {
            if (QFileInfo::exists(path))
                reindexFile(path);
        }
    });
}

CodeIndexer::~CodeIndexer()
{
    stopIndexing();
}

void CodeIndexer::setProjectDir(const QString &dir)
{
    QMutexLocker locker(&m_mutex);
    m_projectDir = dir;
}

void CodeIndexer::startIndexing()
{
    QMutexLocker locker(&m_mutex);
    if (m_indexing) return;
    m_indexing = true;
    emit indexingStarted();
    scanAndIndex();
}

void CodeIndexer::stopIndexing()
{
    QMutexLocker locker(&m_mutex);
    m_indexing = false;
}

bool CodeIndexer::isIndexing() const
{
    return m_indexing;
}

// بازنویسی به صورت غیرمسدودکننده با استفاده از رویدادهای سیستم
void CodeIndexer::scanAndIndex() {
    if (m_projectDir.isEmpty()) return;

    QDirIterator it(m_projectDir, { "*.cpp", "*.h", "*.py", "*.java" }, 
                   QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    
    QStringList allFiles;
    while (it.hasNext()) allFiles << it.next();

    if (allFiles.isEmpty()) {
        emit indexingFinished();
        return;
    }

    m_indexing = true;
    int *currentIndex = new int(0);
    int total = allFiles.size();

    // استفاده از تایمر برای پردازش تدریجی بدون بلاک کردن UI
    QTimer *workerTimer = new QTimer(this);
    connect(workerTimer, &QTimer::timeout, this, [this, allFiles, currentIndex, total, workerTimer]() {
        if (*currentIndex >= total || !m_indexing) {
            workerTimer->stop();
            workerTimer->deleteLater();
            delete currentIndex;
            m_indexing = false;
            emit indexingFinished();
            return;
        }

        QString path = allFiles.at(*currentIndex);
        emit indexingProgress(path, *currentIndex + 1, total);
        
        // پردازش فایل
        indexFile(path);

        (*currentIndex)++;
    });
    
    workerTimer->start(5); // هر ۵ میلی‌ثانیه یک فایل پردازش می‌شود
}

void CodeIndexer::indexFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream ts(&file);
    QString content = ts.readAll();
    file.close();

    QString hash = fileHash(filePath);
    LanguageDetector::Language lang = m_detector->detect(filePath);

    QList<CodeChunk> chunks = chunkContent(content, filePath, lang);

    QMutexLocker locker(&m_mutex);
    m_fileHashes.insert(filePath, hash);
    m_fileChunks.insert(filePath, chunks);
    m_totalChunks += chunks.size();
    locker.unlock();

    QList<SymbolInfo> symbols = m_parser->parseContent(content, filePath, lang);
    m_symbolDb->indexFile(filePath, symbols, hash);

    for (const CodeChunk &chunk : chunks)
        emit fileChunked(chunk);

    emit fileIndexed(filePath, chunks.size(), true);
}

void CodeIndexer::reindexFile(const QString &filePath)
{
    indexFile(filePath);
}

void CodeIndexer::removeFile(const QString &filePath)
{
    QMutexLocker locker(&m_mutex);
    m_fileHashes.remove(filePath);
    m_fileChunks.remove(filePath);
    m_symbolDb->removeFile(filePath);
}

void CodeIndexer::onFileChanged(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    m_changedFiles.insert(path);
    locker.unlock();
    m_debounceTimer->start();
}

void CodeIndexer::onDirChanged(const QString &path)
{
    Q_UNUSED(path);
    m_debounceTimer->start();
}

void CodeIndexer::debounceChanged(const QString &path)
{
    Q_UNUSED(path);
}

QList<CodeIndexer::CodeChunk> CodeIndexer::chunkFile(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QTextStream ts(&file);
    QString content = ts.readAll();
    file.close();

    LanguageDetector::Language lang = m_detector->detect(filePath);
    return chunkContent(content, filePath, lang);
}

QList<CodeIndexer::CodeChunk> CodeIndexer::chunkContent(const QString &content, const QString &filePath,
                                                        LanguageDetector::Language lang)
{
    QList<CodeChunk> chunks;
    QStringList lines = content.split('\n');

    QList<SymbolInfo> symbols = m_parser->parseContent(content, filePath, lang);

    for (const SymbolInfo &sym : symbols) {
        if (sym.endLine < sym.startLine || sym.endLine - sym.startLine > 2000)
            continue;

        CodeChunk chunk;
        chunk.filePath = filePath;
        chunk.language = m_detector->languageName(lang).toLower();
        chunk.className = sym.parentClass;
        chunk.functionName = sym.name;
        chunk.symbolId = QString("%1::%2").arg(filePath, sym.qualifiedName);
        chunk.chunkType = CodeParser::symbolTypeName(sym.type);
        chunk.startLine = sym.startLine;
        chunk.endLine = sym.endLine;
        chunk.text = lines.mid(qMax(0, sym.startLine - 1),
                               sym.endLine - sym.startLine + 1).join('\n');
        chunk.hash = QCryptographicHash::hash(chunk.text.toUtf8(), QCryptographicHash::Sha256).toHex();
        chunk.tokenCount = chunk.text.length() / 4;
        chunks.append(chunk);
    }

    if (chunks.isEmpty() && !lines.isEmpty()) {
        CodeChunk fallback;
        fallback.filePath = filePath;
        fallback.language = m_detector->languageName(lang).toLower();
        fallback.chunkType = "file";
        fallback.startLine = 1;
        fallback.endLine = lines.size();
        fallback.text = content;
        fallback.hash = QCryptographicHash::hash(content.toUtf8(), QCryptographicHash::Sha256).toHex();
        fallback.tokenCount = content.length() / 4;
        chunks.append(fallback);
    }

    return chunks;
}

int CodeIndexer::totalChunks() const
{
    QMutexLocker locker(&m_mutex);
    return m_totalChunks;
}

int CodeIndexer::totalFilesIndexed() const
{
    QMutexLocker locker(&m_mutex);
    return m_fileHashes.size();
}

QStringList CodeIndexer::watchedFiles() const
{
    QMutexLocker locker(&m_mutex);
    return m_fileHashes.keys();
}

QString CodeIndexer::fileHash(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(file.readAll());
    return hash.result().toHex();
}

bool CodeIndexer::hasChanged(const QString &filePath) const
{
    QMutexLocker locker(&m_mutex);
    if (!m_fileHashes.contains(filePath))
        return true;

    QString current = fileHash(filePath);
    return current != m_fileHashes.value(filePath);
}
