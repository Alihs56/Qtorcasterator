#include "planning_engine.h"
#include "api_client.h"
#include "logger.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

PlanningEngine::PlanningEngine(ApiClient *api, MemoryManager *memory, QObject *parent)
    : QObject(parent), m_api(api), m_memory(memory)
{
}

void PlanningEngine::analyze(const QString &query, std::function<void(const PlanResult&)> callback)
{
    QString q = query.toLower().trimmed();

    PlanResult plan;

    // ─── Rule-based intent detection ───

    // Vision / Image
    if (q.contains("image") || q.contains("picture") || q.contains("photo") ||
        q.contains("screenshot") || q.contains("pcb") || q.contains("schematic") ||
        q.contains("ocr") || q.contains("read this") || q.contains("ببین") ||
        q.contains("عکس") || q.contains("تصویر")) {
        plan.intent = Intent::VisionAnalysis;
        plan.needVision = true;
    }

    // Datasheet / Embedded
    if (q.contains("datasheet") || q.contains("manual") || q.contains("pdf") ||
        q.contains("register") || q.contains("peripheral") || q.contains("mcu") ||
        q.contains("microcontroller") || q.contains("adc") || q.contains("uart") ||
        q.contains("spi") || q.contains("i2c") || q.contains("timer") ||
        q.contains("پیچ") || q.contains("داده‌شیت") || q.contains("رجیستر")) {
        plan.intent = Intent::DatasheetAnalysis;
        plan.needRag = true;
    }

    // Code generation / editing
    if (q.contains("generate") || q.contains("write") || q.contains("implement") ||
        q.contains("create") || q.contains("build") || q.contains("تولید") ||
        q.contains("نویس") || q.contains("پیاده")) {
        plan.intent = Intent::CodeGeneration;
        plan.needCode = true;
    }

    // Refactoring
    if (q.contains("refactor") || q.contains("clean") || q.contains("improve") ||
        q.contains("restructure") || q.contains("بازسازی")) {
        plan.intent = Intent::Refactoring;
        plan.needCode = true;
        plan.needRag = true;
    }

    // Debugging / compiler error
    if (q.contains("error") || q.contains("bug") || q.contains("fix") ||
        q.contains("debug") || q.contains("crash") || q.contains("خطا") ||
        q.contains("باگ") || q.contains("اصلاح")) {
        plan.intent = Intent::Debugging;
        plan.needCode = true;
        plan.needRag = true;
    }

    // Qt specific
    if (q.contains("qt") || q.contains("qml") || q.contains("widget") ||
        q.contains("signal") || q.contains("slot") || q.contains("qt creator")) {
        plan.intent = Intent::QtDevelopment;
        plan.needCode = true;
        plan.needRag = true;
    }

    // Default to question answering
    if (plan.intent == Intent::QuestionAnswering && !plan.needRag && !plan.needCode)
        plan.needRag = true;

    // ─── Entity detection from memory ───
    QString device = m_memory->currentDevice();
    if (!device.isEmpty()) {
        plan.entity = device;
    } else if (q.contains("pic18f")) {
        plan.entity = q.section("pic18f", 0, 0).trimmed() + "pic18f" + q.section("pic18f", 1).left(4).toUpper();
        m_memory->setCurrentDevice(plan.entity);
    } else if (q.contains("avr")) {
        plan.entity = "AVR";
    } else if (q.contains("stm32")) {
        plan.entity = "STM32";
    }

    // ─── Complexity detection ───
    int score = 0;
    if (q.length() > 300) score += 2;
    if (q.contains("architecture") || q.contains("system") || q.contains("framework")) score += 2;
    if (q.contains("multiple files") || q.contains("refactor")) score += 2;
    if (q.contains("driver") || q.contains("protocol")) score += 1;

    if (score >= 4)
        plan.complexity = "complex";
    else if (score >= 2)
        plan.complexity = "medium";
    else
        plan.complexity = "simple";

    // ─── Need memory? ───
    if (!m_memory->currentProject().isEmpty() || !m_memory->currentDevice().isEmpty())
        plan.needMemory = true;

    plan.workflow = plan.needVision ? "Vision" :
                    plan.needCode ? (plan.complexity == "complex" ? "Expert+Review" : "Coder+Review") :
                    "RAG+Answer";

    callback(plan);
    emit planReady(plan);
}

bool PlanningEngine::needRag(const QString &query) const
{
    QString q = query.toLower().trimmed();

    QStringList explicitWords = {
        "pdf", "datasheet", "manual", "chapter", "page", "document",
        "this pdf", "this file", "reference", " according to", "spec"
    };

    for (const QString &w : explicitWords) {
        if (q.contains(w))
            return true;
    }

    if (q.length() < 150)
        return true;

    return false;
}
