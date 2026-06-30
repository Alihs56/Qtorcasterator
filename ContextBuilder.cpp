#include "ContextBuilder.h"
#include "LanguageDetector.h"
#include <QDebug>

ContextBuilder::ContextBuilder(SymbolDatabase *symbols, CallGraph *callGraph,
                               DependencyGraph *depGraph, Retriever *retriever, QObject *parent)
    : QObject(parent),
      m_symbols(symbols),
      m_callGraph(callGraph),
      m_depGraph(depGraph),
      m_retriever(retriever)
{
}

void ContextBuilder::build(const QString &userRequest, const QString &intent,
                            const QString &executionPath,
                            const QStringList &symbols,
                            const QStringList &depPaths,
                            const QStringList &chunks,
                            std::function<void(const BuiltContext&)> callback)
{
    BuiltContext ctx = buildSync(userRequest, intent, executionPath, symbols, depPaths, chunks);
    callback(ctx);
}

ContextBuilder::BuiltContext ContextBuilder::buildSync(const QString &userRequest, const QString &intent,
                                                        const QString &executionPath,
                                                        const QStringList &symbolsList,
                                                        const QStringList &depPathsList,
                                                        const QStringList &chunksList)
{
    BuiltContext ctx;
    ctx.userRequest = userRequest;
    ctx.intent = intent;
    ctx.executionPath = executionPath;

    for (const QString &s : symbolsList)
        ctx.relevantSymbols += s + "\n";
    if (!ctx.relevantSymbols.isEmpty())
        ctx.relevantSymbols.prepend("=== RELEVANT SYMBOLS ===\n");

    ctx.retrievedChunks = chunksList.join("\n");
    if (!ctx.retrievedChunks.isEmpty())
        ctx.retrievedChunks.prepend("=== RETRIEVED CHUNKS ===\n");

    ctx.dependencies = depPathsList.join("\n");
    if (!ctx.dependencies.isEmpty())
        ctx.dependencies.prepend("=== DEPENDENCIES ===\n");

    ctx.executionPath = executionPath;
    if (!ctx.executionPath.isEmpty())
        ctx.executionPath.prepend("=== EXECUTION PATH ===\n");

    ctx.fullContext = ctx.userRequest + "\n\n";
    if (!ctx.intent.isEmpty())
        ctx.fullContext += "INTENT: " + ctx.intent + "\n\n";
    ctx.fullContext += ctx.executionPath + "\n";
    ctx.fullContext += ctx.relevantSymbols + "\n";
    ctx.fullContext += ctx.retrievedChunks + "\n";
    ctx.fullContext += ctx.dependencies + "\n";

    ctx.totalTokens = ctx.fullContext.length() / 4;

    emit contextReady(ctx);
    return ctx;
}
