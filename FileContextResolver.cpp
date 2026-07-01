#include "FileContextResolver.h"
#include "SymbolDatabase.h"
#include "CallGraph.h"
#include "DependencyGraph.h"
#include "CodeParser.h"
#include "Retriever.h"
#include <QRegularExpression>

FileContextResolver::FileContextResolver(SymbolDatabase *symbolDb, CallGraph *callGraph,
                                        DependencyGraph *depGraph, Retriever *retriever,
                                        CodeParser *parser, QObject *parent)
    : QObject(parent), m_symbolDb(symbolDb), m_callGraph(callGraph), m_depGraph(depGraph),
      m_retriever(retriever), m_codeParser(parser)
{
}

FileContextResolver::ResolutionResult FileContextResolver::resolve(const QString &query, const QString &currentFile)
{
    ResolutionResult result;

    result.affectedFiles = findAffectedFiles(query, currentFile);
    result.relevantSymbols = findAffectedSymbols(query);
    result.executionPath = traceExecutionPath(query);
    result.relatedDependencies = gatherDependencies(result.affectedFiles);

    result.projectContext = QString("Query: %1\n").arg(query);
    result.projectContext += QString("Current file: %1\n").arg(currentFile);
    result.projectContext += QString("Affected files (%1):\n").arg(result.affectedFiles.size());
    for (const QString &f : result.affectedFiles)
        result.projectContext += "  - " + f + "\n";

    result.projectContext += QString("Relevant symbols (%1):\n").arg(result.relevantSymbols.size());
    for (const QString &s : result.relevantSymbols)
        result.projectContext += "  - " + s + "\n";

    emit resolutionComplete(result);
    return result;
}

QStringList FileContextResolver::findAffectedFiles(const QString &query, const QString &currentFile)
{
    QStringList files;
    if (!currentFile.isEmpty())
        files.append(currentFile);

    QRegularExpression fileRe(R"(\b[\w/\\]+\.(cpp|h|hpp|py|java|js|ts|rs|go|cs)\b)");
    int start = 0;
    QRegularExpressionMatch match;
    while ((start = query.indexOf(fileRe, start, &match)) != -1) {
        files.append(match.captured(0));
        start += match.capturedLength();
    }

    files.removeDuplicates();
    return files;
}

QStringList FileContextResolver::findAffectedSymbols(const QString &query)
{
    QStringList symbols;
    if (!m_symbolDb)
        return symbols;

    QList<SymbolDatabase::SymbolRecord> records = m_symbolDb->searchSymbols(query);
    for (const SymbolDatabase::SymbolRecord &rec : records)
        symbols.append(QString("%1::%2").arg(rec.className, rec.symbolName));

    symbols.removeDuplicates();
    return symbols;
}

QStringList FileContextResolver::traceExecutionPath(const QString &query)
{
    // Extract function names from query
    QStringList path;
    QRegularExpression funcRe(R"(\b([a-zA-Z_][a-zA-Z0-9_]*)\s*\()");
    auto match = funcRe.match(query);
    if (match.hasMatch()) {
        path.append(match.captured(1));
    }
    return path;
}

QStringList FileContextResolver::gatherDependencies(const QStringList &files)
{
    QStringList deps;
    for (const QString &file : files) {
        if (m_depGraph) {
            auto fileDeps = m_depGraph->getTransitiveDependencies(file, 2);
            for (const QString &dep : fileDeps) {
                if (!deps.contains(dep))
                    deps.append(dep);
            }
        }
    }
    return deps;
}
