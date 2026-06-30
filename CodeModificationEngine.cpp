#include "CodeModificationEngine.h"
#include "GitBackupLayer.h"
#include "ProjectIndexer.h"
#include "CodeParser.h"
#include "SymbolDatabase.h"
#include "LanguageDetector.h"
#include "logger.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>

CodeModificationEngine::CodeModificationEngine(CodeParser *parser, SymbolDatabase *symbolDb,
                                               ProjectIndexer *projectIndexer, GitBackupLayer *gitLayer,
                                               CodeVerifier *verifier, ApiClient *api,
                                               QObject *parent)
    : QObject(parent),
      m_parser(parser),
      m_symbolDb(symbolDb),
      m_projectIndexer(projectIndexer),
      m_gitLayer(gitLayer),
      m_verifier(verifier),
      m_api(api)
{
}

CodeModificationEngine::~CodeModificationEngine() = default;

void CodeModificationEngine::setProjectDir(const QString &dir)
{
    QMutexLocker locker(&m_mutex);
    m_projectDir = dir;
}

QString CodeModificationEngine::readFile(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream ts(&file);
    return ts.readAll();
}

// بازنویسی برای نوشتن امن و اتمیک فایل (Atomic Write)
bool CodeModificationEngine::writeFile(const QString &path, const QString &content) {
    QFileInfo fi(path);
    QString tempPath = path + ".tmp"; // ایجاد فایل موقت
    QFile tempFile(tempPath);

    if (!tempFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        LOG_ERROR("Modifier", "Cannot open temp file for writing: " + tempPath);
        return false;
    }

    QTextStream out(&tempFile);
    out.setCodec("UTF-8");
    out << content;
    tempFile.close();

    // اگر فایل اصلی وجود دارد، ابتدا آن را حذف یا جایگزین کن
    if (QFile::exists(path)) {
        if (!QFile::remove(path)) {
            LOG_ERROR("Modifier", "Failed to remove old file: " + path);
            return false;
        }
    }

    // تغییر نام فایل موقت به فایل اصلی (عملیات اتمیک در سطح سیستم‌عامل)
    if (!tempFile.rename(path)) {
        LOG_ERROR("Modifier", "Failed to rename temp file to: " + path);
        return false;
    }

    return true;
}

QString CodeModificationEngine::fileHash(const QString &path) const
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(file.readAll());
    return hash.result().toHex();
}

bool CodeModificationEngine::backupFile(const QString &filePath)
{
    if (!m_gitLayer)
        return true;
    GitBackupLayer::BackupResult result = m_gitLayer->commitBeforeModification(filePath);
    return result.success;
}

bool CodeModificationEngine::restoreBackup(const QString &filePath)
{
    if (!m_gitLayer)
        return false;
    GitBackupLayer::BackupResult result = m_gitLayer->rollback(filePath);
    return result.success;
}

CodeModificationEngine::PatchInfo CodeModificationEngine::classifyOutput(const QString &generatedCode,
                                                                        const QString &filePath)
{
    PatchInfo info;
    info.targetFile = filePath;

    QString trimmed = generatedCode.trimmed();

    if (trimmed.startsWith("---") && trimmed.contains("+++")) {
        info.type = "unified_diff";
        info.diffText = generatedCode;
        return info;
    }

    if (trimmed.contains("```") && trimmed.contains("diff")) {
        info.type = "unified_diff";
        info.diffText = generatedCode;
        return info;
    }

    info.type = "full_file";
    info.newContent = generatedCode;
    return info;
}

bool CodeModificationEngine::validatePatch(const PatchInfo &patch, const QString &currentContent)
{
    Q_UNUSED(patch);
    Q_UNUSED(currentContent);

    if (patch.targetFile.isEmpty())
        return false;

    QFileInfo fi(patch.targetFile);
    if (!fi.exists())
        return false;

    if (patch.type == "full_file") {
        return !patch.newContent.isEmpty();
    }

    if (patch.type == "unified_diff") {
        return !patch.diffText.isEmpty();
    }

    return true;
}

QString CodeModificationEngine::analyzePatch(const QString &generatedCode, const QString &currentCode)
{
    Q_UNUSED(currentCode);
    PatchInfo info = classifyOutput(generatedCode, {});
    return info.type;
}


CodeModificationEngine::ModificationResult CodeModificationEngine::applyPatch(const PatchInfo &patch)
{
    ModificationResult result;
    result.modifiedFile = patch.targetFile;

    if (patch.type == "full_file") {
        if (writeFile(patch.targetFile, patch.newContent)) {
            result.success = true;
            result.patch = "Full file replacement";
        } else {
            result.errorMessage = "Failed to write file";
            result.needsRegeneration = true;
        }
        return result;
    }

    if (patch.type == "unified_diff") {
        result.errorMessage = "Unified diff application not yet implemented";
        result.needsRegeneration = true;
        return result;
    }

    if (patch.type == "new_file") {
        if (writeFile(patch.targetFile, patch.newContent)) {
            result.success = true;
            result.patch = "New file created";
        } else {
            result.errorMessage = "Failed to create new file";
            result.needsRegeneration = true;
        }
        return result;
    }

    if (patch.type == "line_replace") {
        QString currentContent = readFile(patch.targetFile);
        if (currentContent.isEmpty()) {
            result.errorMessage = "Cannot read target file for line replacement";
            result.needsRegeneration = true;
            return result;
        }

        QStringList lines = currentContent.split('\n');
        int changes = qMin(patch.changeStartLines.size(), qMin(patch.changeEndLines.size(), qMin(patch.oldCodeBlocks.size(), patch.newCodeBlocks.size())));

        for (int i = changes - 1; i >= 0; --i) {
            int start = patch.changeStartLines.at(i);
            int end = patch.changeEndLines.at(i);
            if (start < 1 || end < start || end > lines.size()) {
                result.errorMessage = QString("Invalid line range %1-%2 for file with %3 lines").arg(start).arg(end).arg(lines.size());
                result.needsRegeneration = true;
                return result;
            }

            QStringList newLines = patch.newCodeBlocks.at(i).split('\n');
            QStringList before = lines.mid(0, start - 1);
            QStringList after = lines.mid(end);
            lines = before + newLines + after;
        }

        QString newContent = lines.join('\n');
        if (writeFile(patch.targetFile, newContent)) {
            result.success = true;
            result.patch = QString("Applied %1 line replacements").arg(changes);
        } else {
            result.errorMessage = "Failed to write patched file";
            result.needsRegeneration = true;
        }
        return result;
    }

    result.errorMessage = "Unknown patch type: " + patch.type;
    result.needsRegeneration = true;
    return result;
}

CodeModificationEngine::ModificationResult CodeModificationEngine::applyModification(const QString &generatedCode,
                                                                                 const QString &currentCode,
                                                                                 const QString &filePath,
                                                                                 const QString &language) {
    Q_UNUSED(language);
    QMutexLocker locker(&m_mutex);
    PatchInfo info = classifyOutput(generatedCode, filePath);
    if (info.type == "full_file" || info.type == "new_file") {
        info.newContent = generatedCode;
    } else if (info.type == "line_replace" || info.type == "function_replace") {
        info.oldCodeBlocks.clear();
        info.newCodeBlocks.clear();
    }
    ModificationResult result = applyPatch(info);
    if (result.success) {
        emit modificationStarted(filePath);
        emit modificationFinished(result);
    }
    return result;
}
