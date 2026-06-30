#include "ModificationController.h"
#include "SymbolDatabase.h"
#include "CodeParser.h"
#include "GitBackupLayer.h"
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QDebug>

ModificationController::ModificationController(SymbolDatabase *symbolDb, CodeParser *parser,
                                               GitBackupLayer *gitLayer, QObject *parent)
    : QObject(parent),
      m_symbolDb(symbolDb),
      m_parser(parser),
      m_gitLayer(gitLayer)
{
}

ModificationController::DecisionResult ModificationController::evaluate(const QString &generatedCode,
                                                                        const QString &currentCode,
                                                                        const QString &filePath,
                                                                        const QString &language)
{
    DecisionResult result;
    result.targetFile = filePath;

    QFileInfo fi(filePath);
    bool fileExists = fi.exists();

    if (!fileExists) {
        result.decision = AutoApprove;
        result.reason = "New file creation — auto-approved";
        emit decisionMade(result);
        return result;
    }

    QStringList currentFunctions = parseFunctions(currentCode);
    QStringList generatedFunctions = parseFunctions(generatedCode);

    bool hasOverlap = false;
    for (const QString &gf : generatedFunctions) {
        if (currentFunctions.contains(gf)) {
            hasOverlap = true;
            result.targetFunction = gf;
            break;
        }
    }

    if (hasOverlap) {
        result.decision = RequestApproval;
        result.reason = QString("Modification of existing function '%1' in %2 requires approval").arg(result.targetFunction, fi.fileName());
        result.proposedChangeSummary = generatedCode.left(200) + "...";
        emit approvalRequired(result);
    } else {
        result.decision = AutoApprove;
        result.reason = "New functions added — auto-approved";
    }

    emit decisionMade(result);
    return result;
}

bool ModificationController::isNewFileCreation(const QString &generatedCode, const QString &filePath) const
{
    return !QFileInfo::exists(filePath) && !generatedCode.isEmpty();
}

bool ModificationController::isExistingFileModification(const QString &generatedCode, const QString &filePath) const
{
    return QFileInfo::exists(filePath) && !generatedCode.isEmpty();
}

QString ModificationController::extractAffectedFunction(const QString &generatedCode, const QString &currentCode) const
{
    Q_UNUSED(currentCode);
    QRegularExpression funcRe(R"((?:void|int|bool|QString|auto|QList|QMap)\s+(\w+)\s*\([^)]*\))");
    auto match = funcRe.match(generatedCode);
    if (match.hasMatch())
        return match.captured(1);
    return {};
}

bool ModificationController::codeExistsInProject(const QString &filePath) const
{
    return QFileInfo::exists(filePath);
}

QStringList ModificationController::parseFunctions(const QString &code) const
{
    QStringList functions;
    QRegularExpression funcRe(R"((?:void|int|bool|QString|auto|QList|QMap)\s+(\w+)\s*\([^)]*\))");
    int start = 0;
    QRegularExpressionMatch match;
    while ((start = code.indexOf(funcRe, start, &match)) != -1) {
        functions.append(match.captured(1));
        start += match.capturedLength();
    }
    return functions;
}
