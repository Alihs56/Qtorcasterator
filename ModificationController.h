#ifndef MODIFICATIONCONTROLLER_H
#define MODIFICATIONCONTROLLER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class SymbolDatabase;
class CodeParser;
class GitBackupLayer;

class ModificationController : public QObject
{
    Q_OBJECT
public:
    enum Decision {
        AutoApprove,
        RequestApproval,
        Reject
    };

    struct DecisionResult {
        Decision decision = Reject;
        QString reason;
        QString targetFile;
        QString targetFunction;
        QString affectedLines;
        QString proposedChangeSummary;
    };

    explicit ModificationController(SymbolDatabase *symbolDb, CodeParser *parser,
                                    GitBackupLayer *gitLayer, QObject *parent = nullptr);
    ~ModificationController() override = default;

    DecisionResult evaluate(const QString &generatedCode, const QString &currentCode,
                            const QString &filePath, const QString &language);

    bool isNewFileCreation(const QString &generatedCode, const QString &filePath) const;
    bool isExistingFileModification(const QString &generatedCode, const QString &filePath) const;
    QString extractAffectedFunction(const QString &generatedCode, const QString &currentCode) const;

signals:
    void decisionMade(const DecisionResult &result);
    void approvalRequired(const DecisionResult &result);

private:
    bool codeExistsInProject(const QString &filePath) const;
    QStringList parseFunctions(const QString &code) const;

    SymbolDatabase *m_symbolDb = nullptr;
    CodeParser *m_parser = nullptr;
    GitBackupLayer *m_gitLayer = nullptr;
};

#endif // MODIFICATIONCONTROLLER_H
