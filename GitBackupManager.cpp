#include "GitBackupManager.h"
#include "GitBackupLayer.h"

GitBackupManager::GitBackupManager(QObject *parent)
    : QObject(parent)
{
    m_layer = new GitBackupLayer(this);
}

GitBackupManager::~GitBackupManager() = default;

void GitBackupManager::setRepoPath(const QString &path)
{
    m_layer->setRepoPath(path);
}

QString GitBackupManager::repoPath() const
{
    return m_layer->repoPath();
}

GitBackupManager::BackupResult GitBackupManager::commitBeforeModification(const QString &filePath)
{
    BackupResult result;
    GitBackupLayer::BackupResult layerResult = m_layer->commitBeforeModification(filePath);
    result.success = layerResult.success;
    result.commitHash = layerResult.commitHash;
    result.message = layerResult.message;
    result.error = layerResult.error;
    return result;
}

GitBackupManager::BackupResult GitBackupManager::rollback(const QString &filePath)
{
    BackupResult result;
    GitBackupLayer::BackupResult layerResult = m_layer->rollback(filePath);
    result.success = layerResult.success;
    result.commitHash = layerResult.commitHash;
    result.message = layerResult.message;
    result.error = layerResult.error;
    return result;
}

GitBackupManager::BackupResult GitBackupManager::commitAfterModification(const QString &message)
{
    BackupResult result;
    GitBackupLayer::BackupResult layerResult = m_layer->commitAfterModification(message);
    result.success = layerResult.success;
    result.commitHash = layerResult.commitHash;
    result.message = layerResult.message;
    result.error = layerResult.error;
    return result;
}

QString GitBackupManager::lastCommitHash() const
{
    return m_layer->lastCommitHash();
}

bool GitBackupManager::hasUncommittedChanges() const
{
    return m_layer->hasUncommittedChanges();
}

QStringList GitBackupManager::modifiedFiles() const
{
    return m_layer->modifiedFiles();
}
