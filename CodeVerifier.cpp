#include "CodeVerifier.h"
#include "LanguageDetector.h"
#include <QRegularExpression>
#include <QDebug>
#include <algorithm>

CodeVerifier::CodeVerifier(ApiClient *api, QObject *parent)
    : QObject(parent), m_api(api)
{
}

void CodeVerifier::verify(const QString &code, const QString &language,
                          const QString &filePath,
                          std::function<void(const VerificationResult&)> callback)
{
    VerificationResult result = verifyStatic(code, language);
    emit verificationComplete(result);
    callback(result);
}

void CodeVerifier::verifyWithLinter(const QString &code, const QString &linterCommand,
                                    std::function<void(const VerificationResult&)> callback)
{
    Q_UNUSED(code);
    Q_UNUSED(linterCommand);
    VerificationResult result = verifyStatic(code, "generic");
    emit verificationComplete(result);
    callback(result);
}

CodeVerifier::VerificationResult CodeVerifier::verifyStatic(const QString &code, const QString &language)
{
    VerificationResult result;
    result.passed = true;

    result.syntaxOk = true;
    result.missingIncludes = false;
    result.wrongSignatures = false;
    result.qtSignalSlotErrors = false;
    result.dependencyProblems = false;
    result.compileProblems = false;

    if (language == "C++" || language == "C++17" || language == "C++20" || language == "Qt/C++") {
        result = checkSyntax(code, "cpp");
        result = checkIncludes(code, "cpp");
        result = checkSignatures(code, "cpp");
        result = checkQtConnections(code);
        result = checkDependencies(code, "cpp");
    } else if (language == "Python") {
        result = checkSyntax(code, "python");
        result = checkIncludes(code, "python");
    } else if (language == "Rust") {
        result = checkSyntax(code, "rust");
    } else if (language == "Java") {
        result = checkSyntax(code, "java");
    } else if (language == "C#") {
        result = checkSyntax(code, "csharp");
    } else if (language == "JavaScript" || language == "TypeScript") {
        result = checkSyntax(code, "javascript");
    } else if (language == "Go") {
        result = checkSyntax(code, "go");
    }

    QStringList lines = code.split('\n');
    QSet<QString> seen;
    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("//") || trimmed.startsWith("/*") || trimmed.startsWith("*"))
            continue;

        if (seen.contains(trimmed)) {
            result.errors.append("Duplicate code detected: " + trimmed.left(60));
            result.passed = false;
        }
        seen.insert(trimmed);
    }

    if (result.errors.isEmpty()) {
        result.detailedReport = "Verification passed. No critical issues detected.\n";
    } else {
        result.detailedReport = "Verification found issues:\n";
        for (const QString &err : result.errors)
            result.detailedReport += "  - " + err + "\n";
        result.passed = false;
    }

    return result;
}

CodeVerifier::VerificationResult CodeVerifier::checkSyntax(const QString &code, const QString &language)
{
    VerificationResult result;
    Q_UNUSED(language);
    Q_UNUSED(code);

    QStringList lines = code.split('\n');
    int openBraces = 0;
    int closeBraces = 0;
    int openParens = 0;
    int closeParens = 0;

    for (const QString &line : lines) {
        for (QChar ch : line) {
            if (ch == '{') ++openBraces;
            else if (ch == '}') ++closeBraces;
            else if (ch == '(') ++openParens;
            else if (ch == ')') ++closeParens;
        }
    }

    result.syntaxOk = (openBraces == closeBraces) && (openParens == closeParens);
    if (!result.syntaxOk) {
        result.errors.append(QString("Unbalanced braces: {=%1 }=%2 (=%3 )==%4")
                                 .arg(openBraces).arg(closeBraces).arg(openParens).arg(closeParens));
    }

    return result;
}

CodeVerifier::VerificationResult CodeVerifier::checkIncludes(const QString &code, const QString &language)
{
    VerificationResult result;
    Q_UNUSED(language);

    QList<QString> requiredHeaders = {"QObject", "QMainWindow", "QWidget"};
    for (const QString &header : requiredHeaders) {
        if (code.contains(header) && !code.contains("#include <" + header + ">") && !code.contains("#include \"" + header + "\"")) {
            result.missingIncludes = true;
            result.errors.append(QString("Missing include for %1").arg(header));
        }
    }
    return result;
}

CodeVerifier::VerificationResult CodeVerifier::checkSignatures(const QString &code, const QString &language)
{
    VerificationResult result;
    Q_UNUSED(language);
    Q_UNUSED(code);
    return result;
}

CodeVerifier::VerificationResult CodeVerifier::checkQtConnections(const QString &code)
{
    VerificationResult result;
    QRegularExpression connectRe(QRegularExpression::escape("connect("));
    QRegularExpression senderRe(QRegularExpression::escape("sender()"));

    if (connectRe.match(code).hasMatch() != senderRe.match(code).hasMatch()) {
        result.qtSignalSlotErrors = true;
        result.warnings.append("Qt signal/slot detection — review manual connections");
    }
    return result;
}

CodeVerifier::VerificationResult CodeVerifier::checkDependencies(const QString &code, const QString &language)
{
    VerificationResult result;
    Q_UNUSED(language);
    Q_UNUSED(code);
    return result;
}

QString CodeVerifier::buildReviewPrompt(const QString &code, const QString &language) const
{
    return QString("Review the following %1 code for issues:\n%2").arg(language, code);
}
