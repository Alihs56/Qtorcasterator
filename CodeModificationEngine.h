#ifndef CODEMODIFICATIONENGINE_H
#define CODEMODIFICATIONENGINE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <functional>
#include <QMutex>

class CodeParser;
class SymbolDatabase;
class ProjectIndexer;
class GitBackupLayer;
#include "CodeVerifier.h"
class ApiClient;

class CodeModificationEngine : public QObject
{
    Q_OBJECT
public:
    struct ModificationResult {
        bool success = false;
        QString modifiedFile;
        QStringList modifiedFiles;
        QString patch;
        QString errorMessage;
        bool needsRegeneration = false;
        QString verificationReport;
    };

    struct PatchInfo {
        QString type; // "full_file", "unified_diff", "function_replace", "class_modify", "line_replace", "new_file"
        QString targetFile;
        int targetLineStart = -1;
        int targetLineEnd = -1;
        QString targetFunction;
        QString targetClass;
        QString originalHash;
        QString newContent;
        QString diffText;
        QList<int> changeStartLines;
        QList<int> changeEndLines;
        QStringList oldCodeBlocks;
        QStringList newCodeBlocks;
    };

    explicit CodeModificationEngine(CodeParser *parser, SymbolDatabase *symbolDb,
                                    ProjectIndexer *projectIndexer, GitBackupLayer *gitLayer,
                                    CodeVerifier *verifier, ApiClient *api,
                                    QObject *parent = nullptr);
    ~CodeModificationEngine() override;

    void setProjectDir(const QString &dir);

    ModificationResult applyModification(const QString &generatedCode,
                                         const QString &currentCode,
                                         const QString &filePath,
                                         const QString &language);

    ModificationResult applyPatch(const PatchInfo &patch);
    bool validatePatch(const PatchInfo &patch, const QString &currentContent);
    QString analyzePatch(const QString &generatedCode, const QString &currentCode);

    static PatchInfo classifyOutput(const QString &generatedCode, const QString &filePath);

signals:
    void modificationStarted(const QString &filePath);
    void modificationFinished(const ModificationResult &result);
    void backupCreated(const QString &commitHash);
    void rollbackPerformed(const QString &reason);
    void validationFailed(const QString &reason);
    void regenerationRequested(const QString &context);

private:
    bool writeFile(const QString &path, const QString &content);
    QString readFile(const QString &path) const;
    QString fileHash(const QString &path) const;
    bool backupFile(const QString &filePath);
    bool restoreBackup(const QString &filePath);

    CodeParser *m_parser = nullptr;
    SymbolDatabase *m_symbolDb = nullptr;
    ProjectIndexer *m_projectIndexer = nullptr;
    GitBackupLayer *m_gitLayer = nullptr;
    CodeVerifier *m_verifier = nullptr;
    ApiClient *m_api = nullptr;

    QString m_projectDir;
    mutable QMutex m_mutex;
};

#endif // CODEMODIFICATIONENGINE_H
