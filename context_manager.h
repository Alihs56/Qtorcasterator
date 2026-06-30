#ifndef CONTEXT_MANAGER_H
#define CONTEXT_MANAGER_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QList>
#include <functional>

class MemoryManager;
class RetrievalManager;

/**
 * @brief Context Manager — merges all information sources into one unified context.
 *
 * Responsibilities:
 * - Merge Memory + Retrieval + Editor + Images + PDF
 * - Token budgeting
 * - Produce one unified context for Prompt Builder
 * - Never retrieves documents, never builds prompts
 */
class ContextManager : public QObject
{
    Q_OBJECT
public:
    struct UnifiedContext {
        QString memory;
        QString retrieval;
        QString editorCode;
        QString editorSelection;
        QString currentFile;
        QString currentProject;
        QString currentDevice;
        QString currentDatasheet;
        QList<QImage> images;
        QString pdfText;
        QString merged;
        int totalTokens = 0;
        int retrievalTokens = 0;
    };

    explicit ContextManager(MemoryManager *memory, RetrievalManager *retrieval,
                            QObject *parent = nullptr);

    void build(const QString &query, const QString &editorCode,
               const QList<QImage> &images,
               std::function<void(const UnifiedContext&)> callback);

signals:
    void contextReady(const UnifiedContext &context);

private:
    MemoryManager *m_memory;
    RetrievalManager *m_retrieval;
};

#endif
