#ifndef GIT_PANEL_H
#define GIT_PANEL_H

#include <QWidget>
#include <QTreeWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QProcess>
#include <QFileSystemModel>
#include <QTreeView>
#include <QTextEdit>
#include <QTimer>
#include <QMap>
#include <QSet>

class TerminalExecutor;

struct GitStatusEntry {
    QString status;
    QString filePath;
    bool isStaged = false;
};

class GitPanel : public QWidget {
    Q_OBJECT
public:
    explicit GitPanel(TerminalExecutor *executor, QWidget *parent = nullptr);

    void setRepoPath(const QString &path);
    QString repoPath() const;
    void refresh();
    void setDark(bool dark);

signals:
    void diffRequested(const QString &filePath);
    void gitOutput(const QString &text);

private slots:
    void onRefreshClicked();
    void onStageClicked();
    void onUnstageClicked();
    void onCommitClicked();
    void onPushClicked();
    void onPullClicked();
    void onBranchSwitched(int index);
    void onCreateBranch();
    void onStatusDone(int id, int exitCode, const QString &output);
    void onLogDone(int id, int exitCode, const QString &output);
    void onBranchDone(int id, int exitCode, const QString &output);
    void onFileTreeClicked(const QModelIndex &index);
    void onStatusItemDoubleClicked(QListWidgetItem *item);

    // ===== توابع مدیریت وضعیت از نسخه قدیمی =====
    void onCommitDone(int id, int exitCode, const QString &output);
    void onPushDone(int id, int exitCode, const QString &output);
    void onPullDone(int id, int exitCode, const QString &output);

private:
    void setupUI();
    void runGit(const QStringList &args);
    void refreshStatus();
    void refreshLog();
    void refreshBranches();
    void refreshTree();
    void updateRepoInfo();
    void setBusy(bool busy);

    TerminalExecutor *m_executor;
    QString m_repoPath;

    QSplitter *m_mainSplitter;
    QTreeView *m_fileTree;
    QFileSystemModel *m_fileSystemModel;

    QListWidget *m_statusList;
    QLabel *m_statusLabel;
    QLabel *m_branchLabel;
    QComboBox *m_branchCombo;
    QLineEdit *m_commitInput;
    QPushButton *m_commitBtn;
    QPushButton *m_pushBtn;
    QPushButton *m_pullBtn;
    QPushButton *m_stageBtn;
    QPushButton *m_unstageBtn;
    QPushButton *m_refreshBtn;
    QPushButton *m_newBranchBtn;

    QListWidget *m_logList;
    QLabel *m_logLabel;
    QLabel *m_repoInfoLabel;

    QLabel *m_busyIndicator;
    QWidget *m_busyOverlay;
    bool m_dark = true;

    // ===== از نسخه قدیمی: وضعیت‌های در حال اجرا =====
    int m_pendingStatus = -1;
    int m_pendingLog = -1;
    int m_pendingBranch = -1;
    int m_pendingCommit = -1;
    int m_pendingPush = -1;
    int m_pendingPull = -1;

    QList<GitStatusEntry> m_currentStatus;

    void applyStyle();
};

#endif