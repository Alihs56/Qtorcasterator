#ifndef CODEVERIFIER_H
#define CODEVERIFIER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class ApiClient;

class CodeVerifier : public QObject
{
    Q_OBJECT
public:
    struct VerificationResult {
        bool passed = false;
        bool syntaxOk = false;
        bool missingIncludes = false;
        bool wrongSignatures = false;
        bool qtSignalSlotErrors = false;
        bool dependencyProblems = false;
        bool compileProblems = false;
        QStringList errors;
        QStringList warnings;
        QStringList suggestions;
        QString detailedReport;
    };

    explicit CodeVerifier(ApiClient *api, QObject *parent = nullptr);
    ~CodeVerifier() override = default;

    void verify(const QString &code, const QString &language,
                const QString &filePath,
                std::function<void(const VerificationResult&)> callback);

    void verifyWithLinter(const QString &code, const QString &linterCommand,
                          std::function<void(const VerificationResult&)> callback);

    VerificationResult verifyStatic(const QString &code, const QString &language);

signals:
    void verificationComplete(const VerificationResult &result);
    void verificationError(const QString &error);

private:
    VerificationResult checkSyntax(const QString &code, const QString &language);
    VerificationResult checkIncludes(const QString &code, const QString &language);
    VerificationResult checkSignatures(const QString &code, const QString &language);
    VerificationResult checkQtConnections(const QString &code);
    VerificationResult checkDependencies(const QString &code, const QString &language);
    QString buildReviewPrompt(const QString &code, const QString &language) const;

    ApiClient *m_api = nullptr;
};

#endif // CODEVERIFIER_H
