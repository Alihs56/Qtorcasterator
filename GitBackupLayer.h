#ifndef GITBACKUPLAYER_H
#define GITBACKUPLAYER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <QMutex>

class GitManager;
class TerminalExecutor;

class GitBackupLayer : public QObject
{
    Q_OBJECT
public:
    struct BackupResult {
        bool success = false;
        QString commitHash;
        QString message;
        QString error;
    };

    explicit GitBackupLayer(QObject *parent = nullptr);
    ~GitBackupLayer() override;

    void setRepoPath(const QString &path);
    QString repoPath() const;

    BackupResult commitBeforeModification(const QString &filePath = {});
    BackupResult rollback(const QString &filePath = {});
    BackupResult commitAfterModification(const QString &message);

    QString lastCommitHash() const;
    bool hasUncommittedChanges();
    QStringList modifiedFiles();

    static QString generateCommitMessage(const QString &operation, const QString &filePath = {});

signals:
    void backupCreated(const BackupResult &result);
    void rollbackPerformed(const BackupResult &result);
    void gitError(const QString &error);

private:
    bool ensureGitRepo() const;
    bool ensureGitAvailable() const;
    QString runGit(const QStringList &args);
    void initializeExecutor();

    GitManager *m_gitManager = nullptr;
    TerminalExecutor *m_executor = nullptr;
    QString m_repoPath;
    QString m_lastCommitHash;
    mutable QMutex m_mutex;
};

#endif // GITBACKUPLAYER_H
