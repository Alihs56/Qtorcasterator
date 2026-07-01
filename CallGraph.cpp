#include "CallGraph.h"
#include "SymbolDatabase.h"
#include "CodeParser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QMutexLocker>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QDebug>

CallGraph::CallGraph(const QString &dbPath, QObject *parent)
    : QObject(parent), m_dbPath(dbPath)
{
    if (m_dbPath.isEmpty()) {
        m_dbPath = QDir::currentPath() + "/callgraph.db";
    }
}

CallGraph::~CallGraph() {}

bool CallGraph::initialize()
{
    QMutexLocker locker(&m_mutex);

    // Try to establish database connection
    if (QSqlDatabase::contains("callgraph_db")) {
        m_db = QSqlDatabase::database("callgraph_db");
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", "callgraph_db");
        m_db.setDatabaseName(m_dbPath);
    }

    if (!m_db.open()) {
        emit graphError("Cannot open callgraph database");
        return false;
    }

    if (!createTables()) {
        emit graphError("Cannot create tables");
        return false;
    }

    m_ready = true;
    return true;
}

bool CallGraph::isReady() const
{
    QMutexLocker locker(&m_mutex);
    return m_ready;
}

bool CallGraph::createTables()
{
    QSqlQuery query(m_db);
    QString sql = R"(
        CREATE TABLE IF NOT EXISTS call_edges (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            caller TEXT NOT NULL,
            callee TEXT NOT NULL,
            file_path TEXT,
            caller_line INTEGER,
            callee_line INTEGER,
            is_virtual INTEGER DEFAULT 0,
            UNIQUE(caller, callee, file_path)
        )
    )";

    if (!query.exec(sql)) {
        emit graphError("Failed to create table: " + query.lastError().text());
        return false;
    }

    return true;
}

bool CallGraph::ensureConnection() const
{
    if (!m_db.isOpen()) {
        if (!m_db.open()) {
            return false;
        }
    }
    return true;
}

void CallGraph::addCall(const QString &caller, const QString &callee,
                        const QString &filePath, int callerLine, int calleeLine)
{
    QMutexLocker locker(&m_mutex);

    // Add to in-memory graph
    Edge edge;
    edge.caller = caller;
    edge.callee = callee;
    edge.filePath = filePath;
    edge.callerLine = callerLine;
    edge.calleeLine = calleeLine;
    edge.isVirtual = false;

    m_graph[caller].append(edge);
    m_reverseGraph[callee].insert(caller);

    // Store in database
    if (ensureConnection()) {
        QSqlQuery query(m_db);
        query.prepare(R"(
            INSERT OR IGNORE INTO call_edges (caller, callee, file_path, caller_line, callee_line, is_virtual)
            VALUES (?, ?, ?, ?, ?, ?)
        )");
        query.addBindValue(caller);
        query.addBindValue(callee);
        query.addBindValue(filePath);
        query.addBindValue(callerLine);
        query.addBindValue(calleeLine);
        query.addBindValue(0);
        query.exec();
    }
}

void CallGraph::buildFromDatabase(SymbolDatabase *symbolDb)
{
    if (!symbolDb) return;
    QMutexLocker locker(&m_mutex);
    // TODO: Implement building from symbol database
}

void CallGraph::buildFromFile(const QString &filePath)
{
    Q_UNUSED(filePath);
    // TODO: Parse file and build call graph
}

QList<QString> CallGraph::getCallers(const QString &function) const
{
    QMutexLocker locker(&m_mutex);
    QList<QString> callers;
    auto it = m_reverseGraph.find(function);
    if (it != m_reverseGraph.end()) {
        for (const QString &caller : *it) {
            callers.append(caller);
        }
    }
    return callers;
}

QList<QString> CallGraph::getCallees(const QString &function) const
{
    QMutexLocker locker(&m_mutex);
    QList<QString> callees;
    auto it = m_graph.find(function);
    if (it != m_graph.end()) {
        for (const Edge &edge : *it) {
            callees.append(edge.callee);
        }
    }
    return callees;
}

QList<QString> CallGraph::getExecutionPath(const QString &from, const QString &to, int maxDepth) const
{
    QList<CallPath> paths = findAllPaths(from, to, maxDepth);
    if (paths.isEmpty()) return {};
    return paths.first().nodes;
}

QList<QString> CallGraph::getRelatedSymbols(const QString &function, int depth) const
{
    QMutexLocker locker(&m_mutex);
    QSet<QString> related;
    QQueue<QPair<QString, int>> q;
    q.enqueue(qMakePair(function, 0));

    while (!q.isEmpty()) {
        auto [current, d] = q.dequeue();
        if (d >= depth) continue;

        auto it = m_graph.find(current);
        if (it != m_graph.end()) {
            for (const Edge &edge : *it) {
                if (!related.contains(edge.callee)) {
                    related.insert(edge.callee);
                    q.enqueue(qMakePair(edge.callee, d + 1));
                }
            }
        }
    }

    return related.toList();
}

CallGraph::CallPath CallGraph::findLongestPath(const QString &startFunction) const
{
    QMutexLocker locker(&m_mutex);
    CallPath longestPath;
    // TODO: Implement DFS to find longest path
    return longestPath;
}

QList<CallGraph::CallPath> CallGraph::findAllPaths(const QString &from, const QString &to, int maxDepth) const
{
    QMutexLocker locker(&m_mutex);
    QList<CallPath> results;
    QSet<QString> visited;
    QStringList path;
    path.append(from);
    dfsCallees(from, to, visited, path, results, maxDepth);
    return results;
}

void CallGraph::dfsCallers(const QString &current, const QString &target,
                           QSet<QString> &visited, QStringList &path,
                           QList<CallPath> &results, int maxDepth) const
{
    Q_UNUSED(current);
    Q_UNUSED(target);
    Q_UNUSED(visited);
    Q_UNUSED(path);
    Q_UNUSED(results);
    Q_UNUSED(maxDepth);
    // TODO: Implement DFS
}

void CallGraph::dfsCallees(const QString &current, const QString &target,
                           QSet<QString> &visited, QStringList &path,
                           QList<CallPath> &results, int maxDepth) const
{
    if (path.size() > maxDepth) return;
    if (current == target) {
        CallPath p;
        p.nodes = path;
        p.depth = path.size();
        results.append(p);
        return;
    }

    auto it = m_graph.find(current);
    if (it == m_graph.end()) return;

    for (const Edge &edge : *it) {
        if (!visited.contains(edge.callee)) {
            visited.insert(edge.callee);
            path.append(edge.callee);
            dfsCallees(edge.callee, target, visited, path, results, maxDepth);
            path.removeLast();
            visited.remove(edge.callee);
        }
    }
}

void CallGraph::clear()
{
    QMutexLocker locker(&m_mutex);
    m_graph.clear();
    m_reverseGraph.clear();

    if (ensureConnection()) {
        QSqlQuery query(m_db);
        query.exec("DELETE FROM call_edges");
    }
}
