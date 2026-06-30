#include "prompt_builder.h"
#include "planning_engine.h"
#include <QStringList>

PromptBuilder::PromptBuilder(QObject *parent)
    : QObject(parent)
{
}

PromptBuilder::Prompt PromptBuilder::build(const PlanningEngine::PlanResult &plan,
                                            const QString &query,
                                            const QString &context,
                                            const QString &editorCode,
                                            const QList<QImage> &images)
{
    Q_UNUSED(images)

    switch (plan.intent) {
    case PlanningEngine::Intent::VisionAnalysis:
    case PlanningEngine::Intent::PCBAnalysis:
    case PlanningEngine::Intent::SchematicAnalysis:
        return buildVisionPrompt(query, images);
    case PlanningEngine::Intent::CodeGeneration:
    case PlanningEngine::Intent::CodeEditing:
    case PlanningEngine::Intent::Refactoring:
    case PlanningEngine::Intent::Debugging:
    case PlanningEngine::Intent::EmbeddedDevelopment:
    case PlanningEngine::Intent::QtDevelopment:
        return buildEmbeddedPrompt(query, context, editorCode);
    default:
        return buildQAPrompt(query, context);
    }
}

PromptBuilder::Prompt PromptBuilder::buildQAPrompt(const QString &query, const QString &context)
{
    Prompt p;
    p.systemMessage = "You are a technical documentation assistant.\n"
                      "Answer using ONLY the provided reference document.\n"
                      "If the document does not contain the answer, say so explicitly.\n"
                      "Do not use your own knowledge.";

    QString user = "User Question: " + query + "\n\n";
    if (!context.isEmpty())
        user += "Reference Document:\n" + context + "\n\n";
    user += "Answer the question using only the reference document.";

    p.userPrompt = user;
    p.fullPrompt = user;
    return p;
}

PromptBuilder::Prompt PromptBuilder::buildCodePrompt(const QString &query, const QString &context,
                                                     const QString &editorCode)
{
    Prompt p;
    p.systemMessage = "You are a professional C++/Qt developer.\n"
                      "Generate or modify code based on the requirements.\n"
                      "Output complete, compilable code.";

    QString user;
    if (!editorCode.isEmpty())
        user += "Current Code:\n```\n" + editorCode + "\n```\n\n";
    if (!context.isEmpty())
        user += "Reference:\n" + context + "\n\n";
    user += "Request: " + query + "\n\n"
            "Provide the complete modified code.";

    p.userPrompt = user;
    p.fullPrompt = user;
    return p;
}

PromptBuilder::Prompt PromptBuilder::buildVisionPrompt(const QString &query, const QList<QImage> &images)
{
    Q_UNUSED(images)
    Prompt p;
    p.systemMessage = "You are a vision analysis assistant.\n"
                      "Analyze the provided image in detail.\n"
                      "For PCBs: identify components, traces, labels.\n"
                      "For schematics: describe connections and components.\n"
                      "For OCR: extract all text.";

    p.userPrompt = "Analyze this image: " + query;
    p.fullPrompt = p.userPrompt;
    return p;
}

PromptBuilder::Prompt PromptBuilder::buildEmbeddedPrompt(const QString &query, const QString &context,
                                                         const QString &editorCode)
{
    Prompt p;
    p.systemMessage = "You are an embedded systems expert for AVR/PIC/STM32.\n"
                      "Generate accurate register-level C code.\n"
                      "Use only registers and specifications from the reference document.\n"
                      "Never guess register names or addresses.";

    QString user;
    if (!editorCode.isEmpty())
        user += "Current Code:\n```\n" + editorCode + "\n```\n\n";
    if (!context.isEmpty())
        user += "Datasheet Reference:\n" + context + "\n\n";
    user += "Request: " + query + "\n\n"
            "Write the embedded C code based on the datasheet.";

    p.userPrompt = user;
    p.fullPrompt = user;
    return p;
}
