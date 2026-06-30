#ifndef CALLGRAPH_H
#define CALLGRAPH_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QSet>
#include <QQueue>
#include <QSqlDatabase>
#include <QMutex>
#include <functional>

class CodeParser;

class CallGraph : public QObject
{
    Q_OBJECT
public:
    struct Edge {
        QString caller;
        QString callee;
        QString filePath;
        int callerLine = 0;
        int calleeLine = 0;
        bool isVirtual = false;
    };

    struct CallPath {
        QStringList nodes;
        QStringList files;
        int depth = 0;
        QList<Edge> edges;
    };

    explicit CallGraph(const QString &dbPath = QString(), QObject *parent = nullptr);
    ~CallGraph() override;

    bool initialize();
    bool isReady() const;

    void addCall(const QString &caller, const QString &callee,
                 const QString &filePath = {}, int callerLine = 0, int calleeLine = 0);
    void buildFromDatabase(class SymbolDatabase *symbolDb);
    void buildFromFile(const QString &filePath);

    QList<QString> getCallers(const QString &function) const;
    QList<QString> getCallees(const QString &function) const;
    QList<QString> getExecutionPath(const QString &from, const QString &to, int maxDepth = 10) const;
    QList<QString> getRelatedSymbols(const QString &function, int depth = 2) const;

    CallPath findLongestPath(const QString &startFunction) const;
    QList<CallPath> findAllPaths(const QString &from, const QString &to, int maxDepth = 5) const;

    void clear();

signals:
    void pathFound(const QString &from, const QString &to, const CallPath &path);
    void graphError(const QString &error);

private:
    bool createTables();
    bool ensureConnection() const;

    void dfsCallers(const QString &current, const QString &target,
                    QSet<QString> &visited, QStringList &path,
                    QList<CallPath> &results, int maxDepth) const;
    void dfsCallees(const QString &current, const QString &target,
                    QSet<QString> &visited, QStringList &path,
                    QList<CallPath> &results, int maxDepth) const;

    mutable QMutex m_mutex;
    QMap<QString, QList<Edge>> m_graph;
    QMap<QString, QSet<QString>> m_reverseGraph;
    QSqlDatabase m_db;
    QString m_dbPath;
    bool m_ready = false;
};

#endif // CALLGRAPH_H
