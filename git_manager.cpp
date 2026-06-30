#include "git_manager.h"
#include "terminal_executor.h"
#include "logger.h"
#include <QDir>

GitManager::GitManager(TerminalExecutor *executor, QObject *parent)
    : QObject(parent), m_executor(executor) {
    connect(m_executor, &TerminalExecutor::outputReady, this,
            [this](int id, const QString &output) {
                emit gitOutput(id, output);
            });
    connect(m_executor, &TerminalExecutor::finished, this,
            [this](int id, int exitCode, const QString &fullOutput) {
                emit gitFinished(id, exitCode, fullOutput);
            });
}

void GitManager::setRepoPath(const QString &path) {
    m_repoPath = path;
}

QString GitManager::repoPath() const {
    return m_repoPath;
}

// ===== تابع کمکی از نسخه قدیمی =====
bool GitManager::ensureRepoPath() const {
    if (m_repoPath.isEmpty()) {
        LOG_ERROR("Git", "Repository path not set");
        return false;
    }
    QDir dir(m_repoPath);
    if (!dir.exists()) {
        LOG_WARN("Git", "Repository path does not exist: " + m_repoPath);
        return false;
    }
    return true;
}

// ===== پیاده‌سازی executeGit با ساختار جدید =====
int GitManager::executeGit(const QStringList &args) {
    if (!ensureRepoPath()) {  // از نسخه قدیمی
        LOG_ERROR("Git", "Cannot execute git: invalid repo path");
        return -1;
    }
    return m_executor->execute("git", args, m_repoPath);
}

// ===== Repository operations (کامل از نسخه قدیمی) =====
int GitManager::init() {
    LOG_INFO("Git", "Initializing repo in: " + m_repoPath);

    // Create directory if it doesn't exist (از نسخه قدیمی)
    QDir dir(m_repoPath);
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            LOG_ERROR("Git", "Failed to create directory: " + m_repoPath);
            return -1;
        }
    }

    return executeGit({"init"});
}

int GitManager::clone(const QString &url, const QString &path) {
    QString targetPath = path.isEmpty() ? m_repoPath : path;

    if (targetPath.isEmpty()) {
        LOG_ERROR("Git", "No target path specified for clone");
        return -1;
    }

    LOG_INFO("Git", QString("Cloning %1 into %2").arg(url).arg(targetPath));

    // Use m_executor directly (not executeGit) because repo doesn't exist yet
    return m_executor->execute("git", {"clone", url, targetPath}, QDir::currentPath());
}

// ===== Basic operations (کامل از نسخه قدیمی) =====
int GitManager::add(const QStringList &files) {
    if (files.isEmpty()) {
        LOG_WARN("Git", "add called with empty file list");
        return -1;
    }
    QStringList args = {"add"};
    args << files;
    return executeGit(args);
}

int GitManager::addAll() {
    return executeGit({"add", "-A"});
}

int GitManager::commit(const QString &message) {
    if (message.isEmpty()) {
        LOG_WARN("Git", "Commit message is empty");
        return -1;
    }
    LOG_INFO("Git", "Commit: " + message.left(80));
    return executeGit({"commit", "-m", message});
}

int GitManager::status() {
    return executeGit({"status"});
}

int GitManager::diff(const QString &path) {
    QStringList args = {"diff", "--color=never"};
    if (!path.isEmpty()) args << path;
    return executeGit(args);
}

int GitManager::log(int count) {
    if (count <= 0) count = 10;
    return executeGit({"log", QString("--oneline"), QString("-%1").arg(count)});
}

// ===== Remote operations (کامل از نسخه قدیمی) =====
int GitManager::push(const QString &remote, const QString &branch) {
    QStringList args = {"push"};
    if (!remote.isEmpty()) args << remote;
    if (!branch.isEmpty()) args << branch;
    LOG_INFO("Git", QString("Push to %1/%2").arg(remote).arg(branch.isEmpty() ? "default" : branch));
    return executeGit(args);
}

int GitManager::pull(const QString &remote, const QString &branch) {
    QStringList args = {"pull"};
    if (!remote.isEmpty()) args << remote;
    if (!branch.isEmpty()) args << branch;
    LOG_INFO("Git", QString("Pull from %1/%2").arg(remote).arg(branch.isEmpty() ? "default" : branch));
    return executeGit(args);
}

int GitManager::fetch(const QString &remote) {
    LOG_INFO("Git", QString("Fetch from %1").arg(remote));
    return executeGit({"fetch", remote});
}

// ===== Branch operations (کامل از نسخه قدیمی) =====
int GitManager::checkout(const QString &branch) {
    if (branch.isEmpty()) {
        LOG_ERROR("Git", "checkout: branch name is empty");
        return -1;
    }
    LOG_INFO("Git", "Checking out: " + branch);
    return executeGit({"checkout", branch});
}

int GitManager::checkoutNew(const QString &branch) {
    if (branch.isEmpty()) {
        LOG_ERROR("Git", "checkoutNew: branch name is empty");
        return -1;
    }
    LOG_INFO("Git", "Creating and checking out new branch: " + branch);
    return executeGit({"checkout", "-b", branch});
}

int GitManager::branch(const QString &name) {
    QStringList args = {"branch"};
    if (!name.isEmpty()) args << name;
    return executeGit(args);
}

int GitManager::branchDelete(const QString &name) {
    if (name.isEmpty()) {
        LOG_ERROR("Git", "branchDelete: branch name is empty");
        return -1;
    }
    LOG_INFO("Git", "Deleting branch: " + name);
    return executeGit({"branch", "-D", name});
}

int GitManager::merge(const QString &branch) {
    if (branch.isEmpty()) {
        LOG_ERROR("Git", "merge: branch name is empty");
        return -1;
    }
    LOG_INFO("Git", "Merging branch: " + branch);
    return executeGit({"merge", branch});
}

// ===== Utility operations (کامل از نسخه قدیمی) =====
int GitManager::reset(const QString &mode, const QString &target) {
    QString modeLower = mode.toLower();
    if (modeLower != "hard" && modeLower != "soft" && modeLower != "mixed") {
        LOG_WARN("Git", "Invalid reset mode: " + mode + " (using 'hard')");
        modeLower = "hard";
    }
    LOG_INFO("Git", QString("Reset %1 to %2").arg(modeLower).arg(target));
    return executeGit({"reset", QString("--%1").arg(modeLower), target});
}

int GitManager::stash() {
    LOG_INFO("Git", "Stashing changes");
    return executeGit({"stash"});
}

int GitManager::stashPop() {
    LOG_INFO("Git", "Popping stash");
    return executeGit({"stash", "pop"});
}