#ifndef PROMPT_BUILDER_H
#define PROMPT_BUILDER_H

#include <QObject>
#include <QString>
#include "planning_engine.h"

class ApiClient;

/**
 * @brief Prompt Builder — builds prompts based on planner decisions.
 *
 * Responsibilities:
 * - Create prompts for different intents (QA, Code, Vision, PCB, etc.)
 * - Include user request, context, retrieved knowledge, current file, rules
 * - Never decides — Planner decides, Prompt Builder only builds
 */
class PromptBuilder : public QObject
{
    Q_OBJECT
public:
    struct Prompt {
        QString systemMessage;
        QString userPrompt;
        QString fullPrompt;
    };

    explicit PromptBuilder(QObject *parent = nullptr);

    Prompt build(const PlanningEngine::PlanResult &plan,
                 const QString &query,
                 const QString &context,
                 const QString &editorCode,
                 const QList<QImage> &images);

private:
    Prompt buildQAPrompt(const QString &query, const QString &context);
    Prompt buildCodePrompt(const QString &query, const QString &context, const QString &editorCode);
    Prompt buildVisionPrompt(const QString &query, const QList<QImage> &images);
    Prompt buildEmbeddedPrompt(const QString &query, const QString &context, const QString &editorCode);
};

#endif
