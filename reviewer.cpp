#include "reviewer.h"
#include "api_client.h"
#include "logger.h"

Reviewer::Reviewer(ApiClient *api, QObject *parent)
    : QObject(parent), m_api(api)
{
}

void Reviewer::review(const QString &generatedCode, const QString &plan,
                      std::function<void(const ReviewResult&)> callback)
{
    QString prompt = QString(
        "IMPLEMENTATION PLAN:\n%1\n\n"
        "GENERATED CODE:\n```\n%2\n```\n\n"
        "REVIEW CHECKS (Stage 14-15):\n"
        "1. PLAN COMPLIANCE: Does the implementation satisfy every item in the plan?\n"
        "2. CROSS-IMPACT ANALYSIS: Will this break other modules?\n"
        "   - Signals/slots connections still valid?\n"
        "   - Qt parent-child ownership correct?\n"
        "   - Threading remains correct?\n"
        "   - Shared/raw pointer ownership valid?\n"
        "   - Public APIs remain compatible?\n"
        "   - No circular dependencies introduced?\n"
        "3. SELF-VERIFICATION:\n"
        "   - Missing includes or forward declarations?\n"
        "   - Incorrect function signatures?\n"
        "   - Null pointer risks?\n"
        "   - Memory ownership issues?\n"
        "   - Logic errors or unreachable code?\n"
        "   - Backward compatibility preserved?\n\n"
        "Return PASS or FAIL followed by concise explanation of each check.\n"
        "PASS only if implementation satisfies the plan and all checks are green.")
        .arg(plan, generatedCode);

    m_api->sendChatRequest(8001, prompt,
        "You are a senior code reviewer specializing in C++/Qt. "
        "Perform cross-impact analysis and self-verification. "
        "Check plan compliance, module coupling, ownership, threading, and backward compatibility. "
        "Return PASS or FAIL with concise explanation.",
        [this, callback](const AIResponse &res) {
            ReviewResult result;
            if (res.error) {
                result.passed = true;
                result.confidence = 0.5;
                if (callback) callback(result);
                return;
            }

            QString review = res.content;
            if (review.contains("PASS", Qt::CaseInsensitive)) {
                result.passed = true;
                result.confidence = 0.9;
            } else {
                result.passed = false;
                result.confidence = 0.3;
                result.feedback = review;
            }

            emit reviewComplete(result);
            if (callback) callback(result);
        });
}

void Reviewer::reviewWithContext(const QString &generatedCode, const QString &plan,
                                 const QString &languageContext,
                                 std::function<void(const ReviewResult&)> callback)
{
    QString fullCode = generatedCode;
    if (!languageContext.isEmpty())
        fullCode = languageContext + "\n\n" + fullCode;

    QString prompt = QString(
        "IMPLEMENTATION PLAN:\n%1\n\n"
        "GENERATED CODE:\n```\n%2\n```\n\n"
        "REVIEW CHECKS (Stage 14-15):\n"
        "1. PLAN COMPLIANCE: Does the implementation satisfy every item in the plan?\n"
        "2. LANGUAGE COMPLIANCE: Does the code follow the target language's idioms, syntax, and rules?\n"
        "   - C++: modern signal/slot syntax, QPointer usage, RAII, parent-child ownership\n"
        "   - Python: PEP 8, type hints, no manual memory management\n"
        "   - JavaScript/TypeScript: const/let, async/await, strict types\n"
        "   - Rust: ownership, Result/Option, no unsafe blocks\n"
        "   - Go: error handling, context propagation, no raw goroutines\n"
        "3. CROSS-IMPACT ANALYSIS: Will this break other modules?\n"
        "   - Signals/slots connections still valid?\n"
        "   - Qt parent-child ownership correct?\n"
        "   - Threading remains correct?\n"
        "   - Shared/raw pointer ownership valid?\n"
        "   - Public APIs remain compatible?\n"
        "   - No circular dependencies introduced?\n"
        "4. SELF-VERIFICATION:\n"
        "   - Missing includes or forward declarations?\n"
        "   - Incorrect function signatures?\n"
        "   - Null pointer risks?\n"
        "   - Memory ownership issues?\n"
        "   - Logic errors or unreachable code?\n"
        "   - Backward compatibility preserved?\n\n"
        "Return PASS or FAIL followed by concise explanation of each check.\n"
        "PASS only if implementation satisfies the plan and all checks are green.")
        .arg(plan, fullCode);

    m_api->sendChatRequest(8001, prompt,
        "You are a senior code reviewer specializing in C++/Qt and multi-language systems. "
        "Perform cross-impact analysis, self-verification, and language-specific compliance. "
        "Check plan compliance, language idioms, module coupling, ownership, threading, "
        "and backward compatibility. "
        "Return PASS or FAIL with concise explanation.",
        [this, callback](const AIResponse &res) {
            ReviewResult result;
            if (res.error) {
                result.passed = true;
                result.confidence = 0.5;
                if (callback) callback(result);
                return;
            }

            QString review = res.content;
            if (review.contains("PASS", Qt::CaseInsensitive)) {
                result.passed = true;
                result.confidence = 0.9;
            } else {
                result.passed = false;
                result.confidence = 0.3;
                result.feedback = review;
            }

            emit reviewComplete(result);
            if (callback) callback(result);
        });
}
