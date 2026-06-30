#include "CallGraph.h"
#include "SymbolDatabase.h"
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>
#include <algorithm>

CallGraph::CallGraph(const QString &dbPath, QObject *parent)
    : QObject(parent), m_dbPath(dbPath)
{
    if (m_dbPath.isEmpty())
        m_dbPath = QDir::currentPath() + "/callgraph.db";
}

CallGraph::~CallGraph()
{
    clear();
    if (m_db.isOpen())
        m_db.close();
}

bool CallGraph::initialize()
{
    QMutexLocker locker(&m_mutex);
    if (m_ready)
        return true;

    m_db = QSqlDatabase::addDatabase("QSQLITE", "callgraph_db");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        emit graphError("Cannot open call graph database: " + m_db.lastError().text());
        return false;
    }

    if (!createTables()) {
        emit graphError("Failed to create call graph tables: " + m_db.lastError().text());
        return false;
    }

    m_ready = true;
    return true;
}

bool CallGraph::isReady() const
{
    return m_ready;
}

bool CallGraph::createTables()
{
    QSqlQuery query(m_db);
    return query.exec(R"(
        CREATE TABLE IF NOT EXISTS call_edges (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            caller TEXT NOT NULL,
            callee TEXT NOT NULL,
            file_path TEXT DEFAULT '',
            caller_line INTEGER DEFAULT 0,
            callee_line INTEGER DEFAULT 0,
            is_virtual INTEGER DEFAULT 0
        )
    )");
}

bool CallGraph::ensureConnection() const
{
    if (!m_db.isOpen() && !const_cast<CallGraph*>(this)->initialize())
        return false;
    return true;
}

void CallGraph::addCall(const QString &caller, const QString &callee,
                        const QString &filePath, int callerLine, int calleeLine)
{
    QMutexLocker locker(&m_mutex);
    if (!ensureConnection()) return;

    Edge edge;
    edge.caller = caller;
    edge.callee = callee;
    edge.filePath = filePath;
    edge.callerLine = callerLine;
    edge.calleeLine = calleeLine;

    m_graph[caller].append(edge);
    m_reverseGraph[callee].insert(caller);

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO call_edges (caller, callee, file_path, caller_line, callee_line, is_virtual)
        VALUES (?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(caller);
    query.addBindValue(callee);
    query.addBindValue(filePath);
    query.addBindValue(callerLine);
    query.addBindValue(calleeLine);
    query.addBindValue(edge.isVirtual ? 1 : 0);
    query.exec();
}

void CallGraph::buildFromDatabase(class SymbolDatabase *symbolDb)
{
    Q_UNUSED(symbolDb);
    if (!ensureConnection()) return;

    QSqlQuery query(m_db);
    query.exec("SELECT caller, callee, file_path, caller_line, callee_line, is_virtual FROM call_edges");

    while (query.next()) {
        Edge edge;
        edge.caller = query.value(0).toString();
        edge.callee = query.value(1).toString();
        edge.filePath = query.value(2).toString();
        edge.callerLine = query.value(3).toInt();
        edge.calleeLine = query.value(4).toInt();
        edge.isVirtual = query.value(5).toBool();

        m_graph[edge.caller].append(edge);
        m_reverseGraph[edge.callee].insert(edge.caller);
    }
}

void CallGraph::buildFromFile(const QString &filePath)
{
    Q_UNUSED(filePath);
}

QList<QString> CallGraph::getCallers(const QString &function) const
{
    QMutexLocker locker(&m_mutex);
    QList<QString> result;
    if (m_reverseGraph.contains(function))
        result = m_reverseGraph.value(function).values();
    return result;
}

QList<QString> CallGraph::getCallees(const QString &function) const
{
    QMutexLocker locker(&m_mutex);
    QList<QString> result;
    if (m_graph.contains(function)) {
        for (const Edge &e : m_graph.value(function))
            result.append(e.callee);
    }
    return result;
}

QList<QString> CallGraph::getExecutionPath(const QString &from, const QString &to, int maxDepth) const
{
    QMutexLocker locker(&m_mutex);
    QList<QString> result;
    if (!ensureConnection()) return result;

    QSet<QString> visited;
    QStringList path;
    QList<CallPath> paths;
    dfsCallers(from, to, visited, path, paths, maxDepth);

    for (const CallPath &cp : paths) {
        for (const QString &node : cp.nodes)
            result.append(node);
    }
    return result;
}

QList<QString> CallGraph::getRelatedSymbols(const QString &function, int depth) const
{
    QMutexLocker locker(&m_mutex);
    QSet<QString> visited;
    QQueue<QPair<QString, int>> queue;
    QList<QString> result;

    queue.enqueue(qMakePair(function, 0));
    visited.insert(function);

    while (!queue.isEmpty()) {
        auto [current, d] = queue.dequeue();
        result.append(current);

        if (d >= depth) continue;

        if (m_graph.contains(current)) {
            for (const Edge &e : m_graph.value(current)) {
                if (!visited.contains(e.callee)) {
                    visited.insert(e.callee);
                    queue.enqueue(qMakePair(e.callee, d + 1));
                }
            }
        }

        if (m_reverseGraph.contains(current)) {
            for (const QString &caller : m_reverseGraph.value(current)) {
                if (!visited.contains(caller)) {
                    visited.insert(caller);
                    queue.enqueue(qMakePair(caller, d + 1));
                }
            }
        }
    }
    return result;
}

CallGraph::CallPath CallGraph::findLongestPath(const QString &startFunction) const
{
    CallPath longest;
    QSet<QString> visited;
    QStringList path;

    std::function<void(const QString &, int)> dfs = [&](const QString &current, int depth) {
        path.append(current);
        if (depth > longest.depth)
            longest = {path, {}, depth, {}};

        if (m_graph.contains(current)) {
            for (const Edge &e : m_graph.value(current)) {
                if (!visited.contains(e.callee)) {
                    visited.insert(e.callee);
                    dfs(e.callee, depth + 1);
                    visited.remove(e.callee);
                }
            }
        }
        path.removeLast();
    };

    dfs(startFunction, 0);
    return longest;
}

QList<CallGraph::CallPath> CallGraph::findAllPaths(const QString &from, const QString &to, int maxDepth) const
{
    QMutexLocker locker(&m_mutex);
    QList<CallPath> results;
    QSet<QString> visited;
    QStringList path;

    dfsCallers(from, to, visited, path, results, maxDepth);
    return results;
}

void CallGraph::dfsCallers(const QString &current, const QString &target,
                           QSet<QString> &visited, QStringList &path,
                           QList<CallPath> &results, int maxDepth) const
{
    path.append(current);
    visited.insert(current);

    if (current == target || (maxDepth >= 0 && path.size() > static_cast<size_t>(maxDepth))) {
        CallPath cp;
        cp.nodes = path;
        cp.depth = path.size();
        results.append(cp);
        if (current != target) {
            path.removeLast();
            visited.remove(current);
            return;
        }
    }

    if (current != target) {
        if (m_graph.contains(current)) {
            for (const Edge &e : m_graph.value(current)) {
                if (!visited.contains(e.callee))
                    dfsCallers(e.callee, target, visited, path, results, maxDepth);
            }
        }
    }

    path.removeLast();
    visited.remove(current);
}

void CallGraph::dfsCallees(const QString &current, const QString &target,
                           QSet<QString> &visited, QStringList &path,
                           QList<CallPath> &results, int maxDepth) const
{
    path.append(current);
    visited.insert(current);

    if (current == target || (maxDepth >= 0 && path.size() > static_cast<size_t>(maxDepth))) {
        CallPath cp;
        cp.nodes = path;
        cp.depth = path.size();
        results.append(cp);
        if (current != target) {
            path.removeLast();
            visited.remove(current);
            return;
        }
    }

    if (current != target) {
        if (m_reverseGraph.contains(current)) {
            for (const QString &caller : m_reverseGraph.value(current)) {
                if (!visited.contains(caller))
                    dfsCallees(caller, target, visited, path, results, maxDepth);
            }
        }
    }

    path.removeLast();
    visited.remove(current);
}

void CallGraph::clear()
{
    QMutexLocker locker(&m_mutex);
    m_graph.clear();
    m_reverseGraph.clear();
}
