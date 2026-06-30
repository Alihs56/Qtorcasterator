#ifndef REVIEWER_H
#define REVIEWER_H

#include <QObject>
#include <QString>
#include <functional>

class ApiClient;

/**
 * @brief Reviewer — validates answers before returning to user.
 *
 * Responsibilities:
 * - Check completeness, correctness, hallucinations
 * - Verify register names, peripheral names
 * - Check for missing requirements
 * - Correct problems before final answer
 * - Never plans, never retrieves, never executes tools
 */
class Reviewer : public QObject
{
    Q_OBJECT
public:
    struct ReviewResult {
        bool passed = false;
        QString feedback;
        double confidence = 0.0;
    };

    explicit Reviewer(ApiClient *api, QObject *parent = nullptr);

    void review(const QString &generatedCode, const QString &plan,
                std::function<void(const ReviewResult&)> callback);
    void reviewWithContext(const QString &generatedCode, const QString &plan,
                           const QString &languageContext,
                           std::function<void(const ReviewResult&)> callback);

signals:
    void reviewComplete(const ReviewResult &result);
    void reviewError(const QString &error);

private:
    ApiClient *m_api;
};

#endif
