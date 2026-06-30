#ifndef GIT_MANAGER_H
#define GIT_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>

class TerminalExecutor;

class GitManager : public QObject {
    Q_OBJECT
public:
    explicit GitManager(TerminalExecutor *executor, QObject *parent = nullptr);

    void setRepoPath(const QString &path);
    QString repoPath() const;

    // ===== Repository operations (کامل از نسخه قدیمی) =====
    int init();
    int clone(const QString &url, const QString &path = {});

    // ===== Basic operations (کامل از نسخه قدیمی) =====
    int add(const QStringList &files = {});
    int addAll();
    int commit(const QString &message);
    int status();
    int diff(const QString &path = {});
    int log(int count = 10);

    // ===== Remote operations (کامل از نسخه قدیمی) =====
    int push(const QString &remote = "origin", const QString &branch = {});
    int pull(const QString &remote = "origin", const QString &branch = {});
    int fetch(const QString &remote = "origin");

    // ===== Branch operations (کامل از نسخه قدیمی) =====
    int checkout(const QString &branch);
    int checkoutNew(const QString &branch);
    int branch(const QString &name = {});
    int branchDelete(const QString &name);
    int merge(const QString &branch);

    // ===== Utility operations (کامل از نسخه قدیمی) =====
    int reset(const QString &mode = "hard", const QString &target = "HEAD");
    int stash();
    int stashPop();

    // ===== Generic git command (از نسخه جدید با ساختار ساده‌تر) =====
    int executeGit(const QStringList &args);

signals:
    void gitOutput(int id, const QString &output);
    void gitFinished(int id, int exitCode, const QString &fullOutput);
    void gitError(int id, const QString &error);  // از نسخه قدیمی

private:
    bool ensureRepoPath() const;  // از نسخه قدیمی
    TerminalExecutor *m_executor;
    QString m_repoPath;
};

#endif