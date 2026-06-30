#ifndef GITBACKUPMANAGER_H
#define GITBACKUPMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class GitBackupLayer;
class TerminalExecutor;

class GitBackupManager : public QObject
{
    Q_OBJECT
public:
    struct BackupResult {
        bool success = false;
        QString commitHash;
        QString message;
        QString error;
    };

    explicit GitBackupManager(QObject *parent = nullptr);
    ~GitBackupManager() override;

    void setRepoPath(const QString &path);
    QString repoPath() const;

    BackupResult commitBeforeModification(const QString &filePath = {});
    BackupResult rollback(const QString &filePath = {});
    BackupResult commitAfterModification(const QString &message);

    QString lastCommitHash() const;
    bool hasUncommittedChanges() const;
    QStringList modifiedFiles() const;

signals:
    void backupCreated(const BackupResult &result);
    void rollbackPerformed(const BackupResult &result);
    void gitError(const QString &error);

private:
    GitBackupLayer *m_layer = nullptr;
};

#endif // GITBACKUPMANAGER_H
