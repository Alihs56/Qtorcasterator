#include "DependencyGraph.h"
#include "SymbolDatabase.h"
#include "CodeParser.h"
#include "LanguageDetector.h"
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QDirIterator>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QSqlError>
#include <QQueue>
#include <QDebug>

DependencyGraph::DependencyGraph(const QString &dbPath, QObject *parent)
    : QObject(parent), m_dbPath(dbPath)
{
    if (m_dbPath.isEmpty())
        m_dbPath = QDir::currentPath() + "/dependency.db";
}

DependencyGraph::~DependencyGraph()
{
    clear();
    if (m_db.isOpen())
        m_db.close();
}

bool DependencyGraph::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_ready)
        return true;

    m_db = QSqlDatabase::addDatabase("QSQLITE", "dependency_db");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        emit graphError("Cannot open dependency database: " + m_db.lastError().text());
        return false;
    }

    if (!createTables()) {
        emit graphError("Failed to create dependency tables: " + m_db.lastError().text());
        return false;
    }

    m_ready = true;
    return true;
}

bool DependencyGraph::isReady() const
{
    return m_ready;
}

bool DependencyGraph::createTables()
{
    QSqlQuery query(m_db);
    return query.exec(R"(
        CREATE TABLE IF NOT EXISTS dependencies (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            from_symbol TEXT NOT NULL,
            to_symbol TEXT NOT NULL,
            type TEXT DEFAULT 'include',
            source_file TEXT DEFAULT '',
            line INTEGER DEFAULT 0
        )
    )");
}

bool DependencyGraph::ensureConnection()
{
    if (!m_db.isOpen() && !initialize())
        return false;
    return true;
}

void DependencyGraph::addDependency(const Dependency &dep)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return;

    m_dependencies[dep.from].append(dep);
    m_reverseDependencies[dep.to].append(dep);

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO dependencies (from_symbol, to_symbol, type, source_file, line)
        VALUES (?, ?, ?, ?, ?)
    )");
    query.addBindValue(dep.from);
    query.addBindValue(dep.to);
    query.addBindValue(dep.type);
    query.addBindValue(dep.sourceFile);
    query.addBindValue(dep.line);
    query.exec();
}

void DependencyGraph::addFileDependency(const QString &from, const QString &to)
{
    Dependency dep;
    dep.from = from;
    dep.to = to;
    dep.type = "file_dependency";
    dep.sourceFile = from;
    addDependency(dep);
}

void DependencyGraph::addInheritance(const QString &derived, const QString &base)
{
    Dependency dep;
    dep.from = derived;
    dep.to = base;
    dep.type = "inheritance";
    addDependency(dep);
}

void DependencyGraph::addQtConnection(const QString &sender, const QString &signal, const QString &receiver, const QString &slot)
{
    Q_UNUSED(sender);
    Q_UNUSED(signal);
    Q_UNUSED(receiver);
    Q_UNUSED(slot);
}

void DependencyGraph::addTemplate(const QString &templateName, const QString &instantiated)
{
    Dependency dep;
    dep.from = templateName;
    dep.to = instantiated;
    dep.type = "template";
    addDependency(dep);
}

QList<DependencyGraph::Dependency> DependencyGraph::getDependencies(const QString &symbol) const
{
    QMutexLocker locker(&m_mutex);
    return m_dependencies.value(symbol, {});
}

QList<DependencyGraph::Dependency> DependencyGraph::getDependents(const QString &symbol) const
{
    QMutexLocker locker(&m_mutex);
    return m_reverseDependencies.value(symbol, {});
}

QList<QString> DependencyGraph::getTransitiveDependencies(const QString &symbol, int maxDepth) const
{
    QMutexLocker locker(&m_mutex);
    QSet<QString> visited;
    QQueue<QPair<QString, int>> queue;
    QList<QString> result;

    queue.enqueue(qMakePair(symbol, 0));
    visited.insert(symbol);

    while (!queue.isEmpty()) {
        auto [current, depth] = queue.dequeue();
        result.append(current);
        if (depth >= maxDepth) continue;

        if (m_dependencies.contains(current)) {
            for (const Dependency &dep : m_dependencies.value(current)) {
                if (!visited.contains(dep.to)) {
                    visited.insert(dep.to);
                    queue.enqueue(qMakePair(dep.to, depth + 1));
                }
            }
        }
    }
    return result;
}

QList<QString> DependencyGraph::getReverseTransitiveDependencies(const QString &symbol, int maxDepth) const
{
    QMutexLocker locker(&m_mutex);
    QSet<QString> visited;
    QQueue<QPair<QString, int>> queue;
    QList<QString> result;

    queue.enqueue(qMakePair(symbol, 0));
    visited.insert(symbol);

    while (!queue.isEmpty()) {
        auto [current, depth] = queue.dequeue();
        result.append(current);
        if (depth >= maxDepth) continue;

        if (m_reverseDependencies.contains(current)) {
            for (const Dependency &dep : m_reverseDependencies.value(current)) {
                if (!visited.contains(dep.from)) {
                    visited.insert(dep.from);
                    queue.enqueue(qMakePair(dep.from, depth + 1));
                }
            }
        }
    }
    return result;
}

QStringList DependencyGraph::getAffectedFiles(const QString &symbol) const
{
    QSet<QString> files;
    QList<Dependency> deps = getDependencies(symbol);
    for (const Dependency &dep : deps) {
        if (!dep.sourceFile.isEmpty())
            files.insert(dep.sourceFile);
    }
    return files.values();
}

QStringList DependencyGraph::suggestIncludeOrder(const QStringList &files) const
{
    Q_UNUSED(files);
    return files;
}

void DependencyGraph::analyzeProject(const QString &projectDir)
{
    if (!ensureConnection()) return;

    QStringList filters = {"*.cpp", "*.h", "*.hpp", "*.hxx", "*.cc", "*.cxx"};
    QDirIterator it(projectDir, filters, QDir::Files, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();
        scanForIncludes(filePath, filters);
    }
}

void DependencyGraph::clear()
{
    QMutexLocker locker(&m_mutex);
    m_dependencies.clear();
    m_reverseDependencies.clear();
}

void DependencyGraph::scanForIncludes(const QString &filePath, const QStringList &sourceFiles)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream ts(&file);
    QString content = ts.readAll();
    file.close();

    QRegularExpression includeRe(R"(#include\s+[<"]([^>"]+)[>"])");
    int start = 0;
    QRegularExpressionMatch match;
    while ((start = content.indexOf(includeRe, start, &match)) != -1) {
        QString included = match.captured(1);
        for (const QString &src : sourceFiles) {
            if (src.contains(included, Qt::CaseInsensitive) || included.contains(src.mid(2), Qt::CaseInsensitive)) {
                addFileDependency(filePath, included);
                break;
            }
        }
        start += match.capturedLength();
    }
}
