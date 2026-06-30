#ifndef FILECONTEXTRESOLVER_H
#define FILECONTEXTRESOLVER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <functional>

#include "SymbolDatabase.h"
#include "CallGraph.h"
#include "DependencyGraph.h"
#include "Retriever.h"
#include "CodeParser.h"

class FileContextResolver : public QObject
{
    Q_OBJECT
public:
    struct ResolutionResult {
        QStringList affectedFiles;
        QStringList affectedClasses;
        QStringList affectedFunctions;
        QStringList relatedDependencies;
        QStringList executionPath;
        QStringList relevantSymbols;
        QStringList codeChunks;
        QString projectContext;
    };

    explicit FileContextResolver(SymbolDatabase *symbolDb, CallGraph *callGraph,
                                 DependencyGraph *depGraph, Retriever *retriever,
                                 CodeParser *parser, QObject *parent = nullptr);
    ~FileContextResolver() override = default;

    ResolutionResult resolve(const QString &query, const QString &currentFile = {});

signals:
    void resolutionComplete(const ResolutionResult &result);
    void resolutionError(const QString &error);

private:
    QStringList findAffectedFiles(const QString &query, const QString &currentFile);
    QStringList findAffectedSymbols(const QString &query);
    QStringList traceExecutionPath(const QString &query);
    QStringList gatherDependencies(const QStringList &files);

    SymbolDatabase *m_symbolDb = nullptr;
    CallGraph *m_callGraph = nullptr;
    DependencyGraph *m_depGraph = nullptr;
    Retriever *m_retriever = nullptr;
    CodeParser *m_parser = nullptr;
};

#endif // FILECONTEXTRESOLVER_H
