#include "DependencyGraph.h"
#include "SymbolDatabase.h"
#include "CodeParser.h"
#include <QFile>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QTextStream>
#include <QRegularExpression>
#include <QMutexLocker>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlDatabase>
#include <QDebug>

DependencyGraph::DependencyGraph(const QString &dbPath, QObject *parent)
    : QObject(parent), m_dbPath(dbPath)
{
    if (m_dbPath.isEmpty()) {
        m_dbPath = QDir::currentPath() + "/dependency.db";
    }
}

DependencyGraph::~DependencyGraph() {}

bool DependencyGraph::initialize()
{
    QMutexLocker locker(&m_mutex);

    if (QSqlDatabase::contains("dependency_db")) {
        m_db = QSqlDatabase::database("dependency_db");
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", "dependency_db");
        m_db.setDatabaseName(m_dbPath);
    }

    if (!m_db.open()) {
        emit graphError("Cannot open dependency database");
        return false;
    }

    if (!createTables()) {
        emit graphError("Cannot create tables");
        return false;
    }

    m_ready = true;
    return true;
}

bool DependencyGraph::isReady() const
{
    QMutexLocker locker(&m_mutex);
    return m_ready;
}

bool DependencyGraph::createTables()
{
    QSqlQuery query(m_db);
    QString sql = R"(
        CREATE TABLE IF NOT EXISTS dependencies (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            from_symbol TEXT NOT NULL,
            to_symbol TEXT NOT NULL,
            dep_type TEXT,
            source_file TEXT,
            line_number INTEGER,
            UNIQUE(from_symbol, to_symbol, dep_type)
        )
    )";

    if (!query.exec(sql)) {
        emit graphError("Failed to create table: " + query.lastError().text());
        return false;
    }
    return true;
}

bool DependencyGraph::ensureConnection()
{
    if (!m_db.isOpen()) {
        if (!m_db.open()) {
            return false;
        }
    }
    return true;
}

void DependencyGraph::addDependency(const Dependency &dep)
{
    QMutexLocker locker(&m_mutex);
    m_dependencies[dep.from].append(dep);
    m_reverseDependencies[dep.to].append(dep);

    if (ensureConnection()) {
        QSqlQuery query(m_db);
        query.prepare(R"(
            INSERT OR IGNORE INTO dependencies (from_symbol, to_symbol, dep_type, source_file, line_number)
            VALUES (?, ?, ?, ?, ?)
        )");
        query.addBindValue(dep.from);
        query.addBindValue(dep.to);
        query.addBindValue(dep.type);
        query.addBindValue(dep.sourceFile);
        query.addBindValue(dep.line);
        query.exec();
    }
}

void DependencyGraph::addFileDependency(const QString &from, const QString &to)
{
    Dependency dep;
    dep.from = from;
    dep.to = to;
    dep.type = "file";
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

void DependencyGraph::addQtConnection(const QString &sender, const QString &signal,
                                      const QString &receiver, const QString &slot)
{
    Dependency dep;
    dep.from = sender;
    dep.to = receiver;
    dep.type = QString("Qt::%1->%2").arg(signal, slot);
    addDependency(dep);
}

void DependencyGraph::addTemplate(const QString &templateName, const QString &instantiated)
{
    Dependency dep;
    dep.from = templateName;
    dep.to = instantiated;
    dep.type = "template_instantiation";
    addDependency(dep);
}

QList<DependencyGraph::Dependency> DependencyGraph::getDependencies(const QString &symbol) const
{
    QMutexLocker locker(&m_mutex);
    return m_dependencies.value(symbol);
}

QList<DependencyGraph::Dependency> DependencyGraph::getDependents(const QString &symbol) const
{
    QMutexLocker locker(&m_mutex);
    return m_reverseDependencies.value(symbol);
}

QList<QString> DependencyGraph::getTransitiveDependencies(const QString &symbol, int maxDepth) const
{
    QMutexLocker locker(&m_mutex);
    QList<QString> results;
    QSet<QString> visited;
    QQueue<QPair<QString, int>> q;
    q.enqueue(qMakePair(symbol, 0));

    while (!q.isEmpty()) {
        auto [current, depth] = q.dequeue();
        if (depth > maxDepth) continue;
        if (visited.contains(current)) continue;
        visited.insert(current);

        auto it = m_dependencies.find(current);
        if (it != m_dependencies.end()) {
            for (const Dependency &dep : *it) {
                results.append(dep.to);
                if (!visited.contains(dep.to)) {
                    q.enqueue(qMakePair(dep.to, depth + 1));
                }
            }
        }
    }

    return results;
}

QList<QString> DependencyGraph::getReverseTransitiveDependencies(const QString &symbol, int maxDepth) const
{
    QMutexLocker locker(&m_mutex);
    QList<QString> results;
    QSet<QString> visited;
    QQueue<QPair<QString, int>> q;
    q.enqueue(qMakePair(symbol, 0));

    while (!q.isEmpty()) {
        auto [current, depth] = q.dequeue();
        if (depth > maxDepth) continue;
        if (visited.contains(current)) continue;
        visited.insert(current);

        auto it = m_reverseDependencies.find(current);
        if (it != m_reverseDependencies.end()) {
            for (const Dependency &dep : *it) {
                results.append(dep.from);
                if (!visited.contains(dep.from)) {
                    q.enqueue(qMakePair(dep.from, depth + 1));
                }
            }
        }
    }

    return results;
}

QStringList DependencyGraph::getAffectedFiles(const QString &symbol) const
{
    QMutexLocker locker(&m_mutex);
    QStringList files;

    auto it = m_dependencies.find(symbol);
    if (it != m_dependencies.end()) {
        for (const Dependency &dep : *it) {
            if (!dep.sourceFile.isEmpty() && !files.contains(dep.sourceFile)) {
                files.append(dep.sourceFile);
            }
        }
    }

    return files;
}

QStringList DependencyGraph::suggestIncludeOrder(const QStringList &files) const
{
    // Simple topological sort
    QStringList ordered;
    QSet<QString> processed;

    for (const QString &file : files) {
        if (!processed.contains(file)) {
            auto deps = getTransitiveDependencies(file, 3);
            for (const QString &dep : deps) {
                if (files.contains(dep) && !processed.contains(dep)) {
                    ordered.append(dep);
                    processed.insert(dep);
                }
            }
            ordered.append(file);
            processed.insert(file);
        }
    }

    return ordered;
}

void DependencyGraph::analyzeProject(const QString &projectDir)
{
    QMutexLocker locker(&m_mutex);
    QDir dir(projectDir);
    QDirIterator it(dir, QDirIterator::Subdirectories);

    while (it.hasNext()) {
        QString filePath = it.next();
        QFileInfo fi(filePath);
        if (fi.isFile() && (fi.suffix() == "h" || fi.suffix() == "hpp" || fi.suffix() == "cpp")) {
            scanForIncludes(filePath, {});
        }
    }
}

void DependencyGraph::scanForIncludes(const QString &filePath, const QStringList &sourceFiles)
{
    Q_UNUSED(sourceFiles);
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream ts(&file);
    QRegularExpression includeRe(R"(#include\s+[<\"]([^>\"]+)[>\"])");
    int lineNum = 0;

    while (!ts.atEnd()) {
        QString line = ts.readLine();
        lineNum++;
        auto match = includeRe.match(line);
        if (match.hasMatch()) {
            QString includedFile = match.captured(1);
            Dependency dep;
            dep.from = filePath;
            dep.to = includedFile;
            dep.type = "include";
            dep.sourceFile = filePath;
            dep.line = lineNum;
            addDependency(dep);
        }
    }
    file.close();
}

void DependencyGraph::clear()
{
    QMutexLocker locker(&m_mutex);
    m_dependencies.clear();
    m_reverseDependencies.clear();

    if (ensureConnection()) {
        QSqlQuery query(m_db);
        query.exec("DELETE FROM dependencies");
    }
}
