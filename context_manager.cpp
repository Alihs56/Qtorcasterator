#include "context_manager.h"
#include "memory_manager.h"
#include "retrieval_manager.h"
#include "logger.h"

ContextManager::ContextManager(MemoryManager *memory, RetrievalManager *retrieval,
                               QObject *parent)
    : QObject(parent), m_memory(memory), m_retrieval(retrieval)
{
}

void ContextManager::build(const QString &query, const QString &editorCode,
                           const QList<QImage> &images,
                           std::function<void(const UnifiedContext&)> callback)
{
    UnifiedContext ctx;

    // ─── Memory ───
    ctx.currentProject = m_memory->currentProject();
    ctx.currentDevice = m_memory->currentDevice();
    ctx.currentDatasheet = m_memory->currentDatasheet();
    ctx.currentFile = m_memory->currentEditorFile();
    ctx.editorSelection = m_memory->currentSelection();
    ctx.memory = m_memory->conversationSummary();

    // ─── Editor ───
    ctx.editorCode = editorCode;

    // ─── Images ───
    ctx.images = images;

    // ─── Retrieval ───
    m_retrieval->retrieve(query, true, [this, query, editorCode, images, callback](const RetrievalManager::RetrievalResult &res) {
        UnifiedContext ctx;
        ctx.currentProject = m_memory->currentProject();
        ctx.currentDevice = m_memory->currentDevice();
        ctx.currentDatasheet = m_memory->currentDatasheet();
        ctx.currentFile = m_memory->currentEditorFile();
        ctx.editorSelection = m_memory->currentSelection();
        ctx.memory = m_memory->conversationSummary();
        ctx.editorCode = editorCode;
        ctx.images = images;
        ctx.retrieval = res.context;
        ctx.retrievalTokens = res.tokensUsed;
        ctx.totalTokens = res.tokensUsed;

        // ─── Merge ───
        QString merged;
        int tokens = 0;
        const int MAX_TOKENS = 6000;

        auto addSection = [&](const QString &label, const QString &text) {
            if (text.isEmpty()) return;
            int sectionTokens = text.length() / 4;
            if (tokens + sectionTokens > MAX_TOKENS) return;
            merged += label + text + "\n\n";
            tokens += sectionTokens;
        };

        if (!ctx.currentDevice.isEmpty())
            addSection("[DEVICE: ", ctx.currentDevice + "]\n");
        if (!ctx.currentDatasheet.isEmpty())
            addSection("[DATASHEET: ", ctx.currentDatasheet + "]\n");
        if (!ctx.currentProject.isEmpty())
            addSection("[PROJECT: ", ctx.currentProject + "]\n");
        if (!ctx.currentFile.isEmpty())
            addSection("[FILE: ", ctx.currentFile + "]\n");
        if (!ctx.editorSelection.isEmpty())
            addSection("[SELECTION]\n", ctx.editorSelection + "\n");
        if (!ctx.memory.isEmpty())
            addSection("[CONVERSATION]\n", ctx.memory + "\n");
        if (!ctx.retrieval.isEmpty())
            addSection("[REFERENCE]\n", ctx.retrieval + "\n");
        if (!ctx.editorCode.isEmpty())
            addSection("[CURRENT CODE]\n```\n", ctx.editorCode + "\n```\n");

        ctx.merged = merged.trimmed();
        ctx.totalTokens = tokens;

        callback(ctx);
        emit contextReady(ctx);
    });
}
