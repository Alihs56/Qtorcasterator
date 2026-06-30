#ifndef CONTEXTBUILDER_H
#define CONTEXTBUILDER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <functional>

class SymbolDatabase;
class CallGraph;
class DependencyGraph;
class Retriever;

class ContextBuilder : public QObject
{
    Q_OBJECT
public:
    struct BuiltContext {
        QString userRequest;
        QString intent;
        QString executionPath;
        QString relevantSymbols;
        QString functionDefinitions;
        QString relatedClasses;
        QString retrievedChunks;
        QString dependencies;
        QString languageRules;
        QString projectIndex;
        QString architectureAnalysis;
        QString currentFileCode;
        QString fullContext;
        int totalTokens = 0;
    };

    explicit ContextBuilder(SymbolDatabase *symbols, CallGraph *callGraph,
                            DependencyGraph *depGraph, Retriever *retriever,
                            QObject *parent = nullptr);
    ~ContextBuilder() override = default;

    void build(const QString &userRequest, const QString &intent,
               const QString &executionPath,
               const QStringList &symbols,
               const QStringList &depPaths,
               const QStringList &chunks,
               std::function<void(const BuiltContext&)> callback);

    BuiltContext buildSync(const QString &userRequest, const QString &intent,
                           const QString &executionPath,
                           const QStringList &symbols,
                           const QStringList &depPaths,
                           const QStringList &chunks);

signals:
    void contextReady(const BuiltContext &context);
    void buildError(const QString &error);

private:
    SymbolDatabase *m_symbols = nullptr;
    CallGraph *m_callGraph = nullptr;
    DependencyGraph *m_depGraph = nullptr;
    Retriever *m_retriever = nullptr;
};

#endif // CONTEXTBUILDER_H
