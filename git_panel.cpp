#include "git_panel.h"
#include "terminal_executor.h"
#include <QDir>
#include <QApplication>
#include <QStyle>
#include <QHeaderView>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileInfo>
#include <QDateTime>
#include <QScrollBar>

GitPanel::GitPanel(TerminalExecutor *executor, QWidget *parent)
    : QWidget(parent), m_executor(executor) {
    setupUI();
}

void GitPanel::setupUI() {
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 4, 4, 4);
    root->setSpacing(4);

    // ── Repo info bar ──
    auto *infoBar = new QHBoxLayout();
    infoBar->setSpacing(6);

    m_repoInfoLabel = new QLabel("\U0001F4C1  No repository selected");
    m_repoInfoLabel->setToolTip("Path to the current Git repository.\n"
                                "Set via the Run menu or auto-detected from the project.");
    m_repoInfoLabel->setStyleSheet("font-size: 11px; font-weight: bold; padding: 2px;");

    m_busyIndicator = new QLabel("\u23F3  Working...");
    m_busyIndicator->setStyleSheet("font-size: 11px; font-weight: bold;");
    m_busyIndicator->setVisible(false);

    m_refreshBtn = new QPushButton("\u21BB");
    m_refreshBtn->setToolTip("Refresh all Git data (status, log, branches, file tree).\n"
                             "Shortcut: Re-fetches the current state of the repository.");
    m_refreshBtn->setFixedSize(28, 28);
    m_refreshBtn->setStyleSheet("font-size: 16px; font-weight: bold;");
    connect(m_refreshBtn, &QPushButton::clicked, this, &GitPanel::onRefreshClicked);

    infoBar->addWidget(m_repoInfoLabel, 1);
    infoBar->addWidget(m_busyIndicator);
    infoBar->addWidget(m_refreshBtn);
    root->addLayout(infoBar);

    // ── Splitter: top = tree + status, bottom = log ──
    m_mainSplitter = new QSplitter(Qt::Vertical);
    m_mainSplitter->setHandleWidth(1);

    auto *topWidget = new QWidget();
    auto *topLayout = new QVBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(4);

    // --- Branch bar ---
    auto *branchBar = new QHBoxLayout();
    branchBar->setSpacing(4);

    auto *branchIcon = new QLabel("\U0001F9F0");
    branchIcon->setToolTip("Current Git branch.");
    branchIcon->setFixedWidth(20);

    m_branchLabel = new QLabel("--");
    m_branchLabel->setToolTip("The active branch name.\n"
                              "Click the dropdown below to switch branches.");
    m_branchLabel->setObjectName("gitBranchLabel");
    m_branchLabel->setStyleSheet("font-weight: bold; font-size: 12px; padding: 2px 6px; "
                                  "border-radius: 3px;");

    m_branchCombo = new QComboBox();
    m_branchCombo->setToolTip("Select a branch to check out.\n"
                              "The currently checked-out branch is shown at the top.");
    m_branchCombo->setMinimumWidth(80);
    m_branchCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    connect(m_branchCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GitPanel::onBranchSwitched);

    m_newBranchBtn = new QPushButton("+");
    m_newBranchBtn->setToolTip("Create a new branch from the current HEAD.\n"
                               "You will be prompted for a branch name.");
    m_newBranchBtn->setFixedSize(26, 26);
    m_newBranchBtn->setStyleSheet("font-size: 14px; font-weight: bold;");
    connect(m_newBranchBtn, &QPushButton::clicked, this, &GitPanel::onCreateBranch);

    branchBar->addWidget(branchIcon);
    branchBar->addWidget(m_branchLabel);
    branchBar->addWidget(m_branchCombo, 1);
    branchBar->addWidget(m_newBranchBtn);
    topLayout->addLayout(branchBar);

    // --- Tree + Status side by side ---
    auto *midSplitter = new QSplitter(Qt::Horizontal);
    midSplitter->setHandleWidth(1);

    // File tree
    auto *treeContainer = new QWidget();
    auto *treeLayout = new QVBoxLayout(treeContainer);
    treeLayout->setContentsMargins(0, 0, 0, 0);
    treeLayout->setSpacing(2);

    auto *treeHeader = new QLabel("\U0001F4C4  Repository Files");
    treeHeader->setToolTip("Browse all files in the repository tree.\n"
                           "Double-click a file to open it in the editor.");
    treeHeader->setObjectName("gitHeader");
    treeHeader->setStyleSheet("font-weight: bold; font-size: 10px; padding: 2px 4px; "
                                "border-radius: 2px;");

    m_fileSystemModel = new QFileSystemModel(this);
    m_fileSystemModel->setFilter(QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files);
    m_fileSystemModel->setRootPath(QDir::rootPath());

    m_fileTree = new QTreeView();
    m_fileTree->setToolTip("Repository file tree.\n"
                           "Click to navigate. Double-click to open a file for editing.");
    m_fileTree->setModel(m_fileSystemModel);
    m_fileTree->setRootIndex(QModelIndex());
    m_fileTree->setAnimated(true);
    m_fileTree->setIndentation(14);
    m_fileTree->setSortingEnabled(true);
    m_fileTree->header()->setStretchLastSection(true);
    m_fileTree->header()->setSectionHidden(1, true);
    m_fileTree->header()->setSectionHidden(2, true);
    m_fileTree->header()->setSectionHidden(3, true);
    connect(m_fileTree, &QTreeView::clicked, this, &GitPanel::onFileTreeClicked);

    treeLayout->addWidget(treeHeader);
    treeLayout->addWidget(m_fileTree, 1);

    // Status list
    auto *statusContainer = new QWidget();
    auto *statusLayout = new QVBoxLayout(statusContainer);
    statusLayout->setContentsMargins(0, 0, 0, 0);
    statusLayout->setSpacing(2);

    auto *statusHeader = new QHBoxLayout();
    m_statusLabel = new QLabel("\u2691  Changes");
    m_statusLabel->setToolTip("Files with uncommitted changes.\n"
                               "Color legend: \u2714 Staged  \u270F Modified  \u2795 Untracked");
    m_statusLabel->setObjectName("gitHeader");
    m_statusLabel->setStyleSheet("font-weight: bold; font-size: 10px; padding: 2px 4px; "
                                  "border-radius: 2px;");

    auto *actionRow = new QHBoxLayout();
    actionRow->setSpacing(2);

    m_stageBtn = new QPushButton("Stage");
    m_stageBtn->setToolTip("Stage selected file(s) for commit.\n"
                           "Staged files will be included in the next commit.");
    m_stageBtn->setFixedHeight(22);
    m_stageBtn->setStyleSheet("font-size: 10px; padding: 0 8px;");

    m_unstageBtn = new QPushButton("Unstage");
    m_unstageBtn->setToolTip("Unstage selected file(s).\n"
                             "Move them back to the modified list.");
    m_unstageBtn->setFixedHeight(22);
    m_unstageBtn->setStyleSheet("font-size: 10px; padding: 0 8px;");

    connect(m_stageBtn, &QPushButton::clicked, this, &GitPanel::onStageClicked);
    connect(m_unstageBtn, &QPushButton::clicked, this, &GitPanel::onUnstageClicked);

    actionRow->addWidget(m_stageBtn);
    actionRow->addWidget(m_unstageBtn);
    actionRow->addStretch();

    statusHeader->addWidget(m_statusLabel, 1);
    statusLayout->addLayout(statusHeader);
    statusLayout->addLayout(actionRow);

    m_statusList = new QListWidget();
    m_statusList->setToolTip("Git status entries.\n"
                              "Double-click to view the diff for a file.\n"
                              "Keyboard: select then press Stage/Unstage.");
    m_statusList->setAlternatingRowColors(true);
    m_statusList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    connect(m_statusList, &QListWidget::itemDoubleClicked,
            this, &GitPanel::onStatusItemDoubleClicked);

    statusLayout->addWidget(m_statusList, 1);

    midSplitter->addWidget(treeContainer);
    midSplitter->addWidget(statusContainer);
    midSplitter->setStretchFactor(0, 1);
    midSplitter->setStretchFactor(1, 1);

    topLayout->addWidget(midSplitter, 1);

    // --- Commit bar ---
    auto *commitBar = new QHBoxLayout();
    commitBar->setSpacing(4);

    m_commitInput = new QLineEdit();
    m_commitInput->setToolTip("Enter a commit message and press Commit.\n"
                              "All staged changes will be committed with this message.");
    m_commitInput->setPlaceholderText("Commit message...");
    m_commitInput->setStyleSheet("font-size: 11px; padding: 4px 6px;");

    m_commitBtn = new QPushButton("Commit");
    m_commitBtn->setToolTip("Commit all staged changes with the message above.\n"
                            "Shortcut: Enter while the message field is focused.");
    m_commitBtn->setFixedHeight(28);
    m_commitBtn->setStyleSheet("font-weight: bold; font-size: 11px; padding: 0 12px;");
    connect(m_commitBtn, &QPushButton::clicked, this, &GitPanel::onCommitClicked);
    connect(m_commitInput, &QLineEdit::returnPressed, this, &GitPanel::onCommitClicked);

    m_pushBtn = new QPushButton("\u2191 Push");
    m_pushBtn->setToolTip("Push commits to the remote repository (origin).");
    m_pushBtn->setFixedHeight(28);
    m_pushBtn->setStyleSheet("font-size: 10px; padding: 0 10px;");
    connect(m_pushBtn, &QPushButton::clicked, this, &GitPanel::onPushClicked);

    m_pullBtn = new QPushButton("\u2193 Pull");
    m_pullBtn->setToolTip("Pull latest changes from the remote repository (origin).");
    m_pullBtn->setFixedHeight(28);
    m_pullBtn->setStyleSheet("font-size: 10px; padding: 0 10px;");
    connect(m_pullBtn, &QPushButton::clicked, this, &GitPanel::onPullClicked);

    commitBar->addWidget(m_commitInput, 1);
    commitBar->addWidget(m_commitBtn);
    commitBar->addWidget(m_pushBtn);
    commitBar->addWidget(m_pullBtn);
    topLayout->addLayout(commitBar);

    m_mainSplitter->addWidget(topWidget);

    // --- Log section (bottom) ---
    auto *logContainer = new QWidget();
    auto *logLayout = new QVBoxLayout(logContainer);
    logLayout->setContentsMargins(0, 0, 0, 0);
    logLayout->setSpacing(2);

    m_logLabel = new QLabel("\U0001F4CB  Commit History");
    m_logLabel->setToolTip("Recent commit history for the current branch.\n"
                           "Shows hash, author, date, and message for each commit.");
    m_logLabel->setObjectName("gitHeader");
    m_logLabel->setStyleSheet("font-weight: bold; font-size: 10px; padding: 2px 4px; "
                                "border-radius: 2px;");

    m_logList = new QListWidget();
    m_logList->setToolTip("Double-click a commit to see its full diff.\n"
                          "Each entry shows: short hash | author | date | message");
    m_logList->setAlternatingRowColors(true);
    m_logList->setWordWrap(true);
    m_logList->setSpacing(2);

    logLayout->addWidget(m_logLabel);
    logLayout->addWidget(m_logList, 1);

    m_mainSplitter->addWidget(logContainer);
    m_mainSplitter->setStretchFactor(0, 3);
    m_mainSplitter->setStretchFactor(1, 1);

    root->addWidget(m_mainSplitter, 1);

    m_dark = true;
    applyStyle();
    setBusy(false);
}

void GitPanel::setDark(bool dark) {
    m_dark = dark;
    applyStyle();
}

void GitPanel::applyStyle() {
    if (m_dark) {
        setStyleSheet(
            "QTreeView, QListWidget {"
            "  background: #1e2229; color: #abb2bf; border: 1px solid #3e4452;"
            "  border-radius: 3px; font-size: 11px;"
            "}"
            "QTreeView::item:hover, QListWidget::item:hover {"
            "  background: #2c313a;"
            "}"
            "QTreeView::item:selected, QListWidget::item:selected {"
            "  background: #3e4452; color: #e5c07b;"
            "}"
            "QComboBox {"
            "  background: #1e2229; color: #abb2bf; border: 1px solid #3e4452;"
            "  border-radius: 3px; padding: 2px 6px; font-size: 11px;"
            "}"
            "QComboBox::drop-down { border: none; }"
            "QComboBox QAbstractItemView {"
            "  background: #1e2229; color: #abb2bf; selection-background-color: #3e4452;"
            "}"
            "QLineEdit {"
            "  background: #1e2229; color: #abb2bf; border: 1px solid #3e4452;"
            "  border-radius: 3px;"
            "}"
            "QPushButton {"
            "  background: #2c313a; color: #abb2bf; border: 1px solid #3e4452;"
            "  border-radius: 3px;"
            "}"
            "QPushButton:hover { background: #3e4452; color: #e5c07b; }"
            "QPushButton:pressed { background: #4b5263; }"
            "QPushButton:disabled { color: #5c6370; background: #1e2229; }"
            "QHeaderView::section {"
            "  background: #2c313a; color: #abb2bf; border: 1px solid #3e4452; padding: 2px;"
            "}"
            "#gitHeader, #gitBranchLabel {"
            "  background: #2c313a; color: #abb2bf;"
            "}"
        );
    } else {
        setStyleSheet(
            "QTreeView, QListWidget {"
            "  background: #ffffff; color: #333333; border: 1px solid #cccccc;"
            "  border-radius: 3px; font-size: 11px;"
            "}"
            "QTreeView::item:hover, QListWidget::item:hover {"
            "  background: #e8f0fe;"
            "}"
            "QTreeView::item:selected, QListWidget::item:selected {"
            "  background: #cce5ff; color: #1a73e8;"
            "}"
            "QComboBox {"
            "  background: #ffffff; color: #333333; border: 1px solid #cccccc;"
            "  border-radius: 3px; padding: 2px 6px; font-size: 11px;"
            "}"
            "QComboBox::drop-down { border: none; }"
            "QComboBox QAbstractItemView {"
            "  background: #ffffff; color: #333333; selection-background-color: #cce5ff;"
            "}"
            "QLineEdit {"
            "  background: #ffffff; color: #333333; border: 1px solid #cccccc;"
            "  border-radius: 3px;"
            "}"
            "QPushButton {"
            "  background: #f0f0f0; color: #333333; border: 1px solid #cccccc;"
            "  border-radius: 3px;"
            "}"
            "QPushButton:hover { background: #e8f0fe; color: #1a73e8; }"
            "QPushButton:pressed { background: #cce5ff; }"
            "QPushButton:disabled { color: #aaaaaa; background: #f5f5f5; }"
            "QHeaderView::section {"
            "  background: #f0f0f0; color: #333333; border: 1px solid #cccccc; padding: 2px;"
            "}"
            "#gitHeader, #gitBranchLabel {"
            "  background: #f0f0f0; color: #333333;"
            "}"
        );
    }
}

void GitPanel::setRepoPath(const QString &path) {
    if (path == m_repoPath) return;
    m_repoPath = path;
    updateRepoInfo();
    refresh();
}

QString GitPanel::repoPath() const {
    return m_repoPath;
}

void GitPanel::refresh() {
    if (m_repoPath.isEmpty()) return;
    setBusy(true);
    m_statusList->clear();
    m_logList->clear();
    m_branchCombo->clear();
    refreshBranches();
    refreshStatus();
    refreshLog();
    refreshTree();
}

void GitPanel::updateRepoInfo() {
    QString errorColor = m_dark ? "#e06c75" : "#d32f2f";
    QString warnColor  = m_dark ? "#e5c07b" : "#e65100";
    QString okColor    = m_dark ? "#98c379" : "#2e7d32";
    if (m_repoPath.isEmpty()) {
        m_repoInfoLabel->setText("\U0001F4C1  No repository selected");
        m_repoInfoLabel->setStyleSheet(QString("font-size: 11px; font-weight: bold; padding: 2px; color: %1;").arg(errorColor));
        return;
    }
    QFileInfo fi(m_repoPath);
    QString repoName = fi.isDir() ? fi.fileName() : fi.absolutePath();
    QDir gitDir(m_repoPath + "/.git");
    if (!gitDir.exists()) {
        m_repoInfoLabel->setText(QString("\u26A0  %1 (not a git repo)").arg(repoName));
        m_repoInfoLabel->setStyleSheet(QString("font-size: 11px; font-weight: bold; padding: 2px; color: %1;").arg(warnColor));
        return;
    }
    m_repoInfoLabel->setText(QString("\U0001F4C1  %1").arg(repoName));
    m_repoInfoLabel->setStyleSheet(QString("font-size: 11px; font-weight: bold; padding: 2px; color: %1;").arg(okColor));
}

void GitPanel::refreshTree() {
    if (m_repoPath.isEmpty()) return;
    m_fileSystemModel->setRootPath(m_repoPath);
    m_fileTree->setRootIndex(m_fileSystemModel->index(m_repoPath));
    m_fileTree->header()->setSectionHidden(1, true);
    m_fileTree->header()->setSectionHidden(2, true);
    m_fileTree->header()->setSectionHidden(3, true);
}

void GitPanel::runGit(const QStringList &args) {
    if (m_repoPath.isEmpty()) return;
    m_executor->execute("git", args, m_repoPath);
}

void GitPanel::setBusy(bool busy) {
    m_busyIndicator->setVisible(busy);
    m_commitBtn->setEnabled(!busy);
    m_pushBtn->setEnabled(!busy);
    m_pullBtn->setEnabled(!busy);
    m_stageBtn->setEnabled(!busy);
    m_unstageBtn->setEnabled(!busy);
    m_refreshBtn->setEnabled(!busy);
    m_newBranchBtn->setEnabled(!busy);
    m_branchCombo->setEnabled(!busy);
    m_commitInput->setEnabled(!busy);
}

void GitPanel::onRefreshClicked() {
    refresh();
}

// ===== Status ============================================

void GitPanel::refreshStatus() {
    if (m_repoPath.isEmpty()) return;
    if (m_pendingStatus >= 0) return;
    m_pendingStatus = m_executor->execute(
        "git", {"status", "--porcelain", "--branch"}, m_repoPath, 15000);
    connect(m_executor, &TerminalExecutor::finished, this, [this](int id, int exitCode, const QString &output) {
        if (id == m_pendingStatus) {
            onStatusDone(id, exitCode, output);
        }
    });
}

void GitPanel::onStatusDone(int id, int exitCode, const QString &output) {
    if (id != m_pendingStatus) return;
    m_pendingStatus = -1;
    m_statusList->clear();
    m_currentStatus.clear();

    if (exitCode != 0) {
        m_statusLabel->setText("\u2691  Changes (error)");
        setBusy(false);
        return;
    }

    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    int modifiedCount = 0, stagedCount = 0, untrackedCount = 0;

    for (const QString &line : lines) {
        if (line.startsWith("##")) {
            QString branchInfo = line.mid(2).trimmed();
            QString branchName = branchInfo.section("...", 0, 0).trimmed();
            m_branchLabel->setText(branchName);
            continue;
        }
        if (line.length() < 3) continue;

        QString xy = line.left(2);
        QString filePath = line.mid(3).trimmed();

        GitStatusEntry entry;
        entry.filePath = filePath;
        entry.isStaged = (xy[0] != ' ' && xy[0] != '?' && xy[0] != '!');

        char statusChar = (xy[1] != ' ') ? xy[1].toLatin1() : xy[0].toLatin1();
        entry.status = QString(statusChar);
        m_currentStatus.append(entry);

        QString icon;
        if (entry.isStaged) {
            icon = "\u2714";
            stagedCount++;
        } else if (statusChar == '?' || statusChar == '!') {
            icon = "\u2795";
            untrackedCount++;
        } else {
            icon = "\u270F";
            modifiedCount++;
        }

        auto *item = new QListWidgetItem(QString(" %1  %2").arg(icon, filePath));
        item->setData(Qt::UserRole, filePath);
        item->setData(Qt::UserRole + 1, entry.isStaged);

        QString stagedColor  = m_dark ? "#98c379" : "#2e7d32";
        QString untrackColor = m_dark ? "#61afef" : "#1565c0";
        QString modColor    = m_dark ? "#e5c07b" : "#e65100";
        QString fg = entry.isStaged ? stagedColor : (statusChar == '?' ? untrackColor : modColor);
        item->setForeground(QColor(fg));
        item->setToolTip(QString("File: %1\nStatus: %2\nDouble-click to view diff")
                            .arg(filePath, entry.isStaged ? "Staged" : (statusChar == '?' ? "Untracked" : "Modified")));
        m_statusList->addItem(item);
    }

    QString summary = QString("\u2691  Changes  [%1 staged  %2 modified  %3 untracked]")
                          .arg(stagedCount).arg(modifiedCount).arg(untrackedCount);
    m_statusLabel->setText(summary);
    setBusy(false);
}

void GitPanel::onStageClicked() {
    auto selected = m_statusList->selectedItems();
    if (selected.isEmpty()) return;
    QStringList files;
    for (auto *item : selected) {
        bool isStaged = item->data(Qt::UserRole + 1).toBool();
        if (!isStaged) {
            files << item->data(Qt::UserRole).toString();
        }
    }
    if (files.isEmpty()) return;
    QStringList args = {"add"};
    args << files;
    runGit(args);
    QTimer::singleShot(500, this, &GitPanel::refreshStatus);
}

void GitPanel::onUnstageClicked() {
    auto selected = m_statusList->selectedItems();
    if (selected.isEmpty()) return;
    QStringList files;
    for (auto *item : selected) {
        bool isStaged = item->data(Qt::UserRole + 1).toBool();
        if (isStaged) {
            files << item->data(Qt::UserRole).toString();
        }
    }
    if (files.isEmpty()) return;
    QStringList args = {"restore", "--staged"};
    args << files;
    runGit(args);
    QTimer::singleShot(500, this, &GitPanel::refreshStatus);
}

void GitPanel::onStatusItemDoubleClicked(QListWidgetItem *item) {
    if (!item) return;
    QString filePath = item->data(Qt::UserRole).toString();
    emit diffRequested(filePath);
}

// ===== Commit با مدیریت کامل از نسخه قدیمی =====

void GitPanel::onCommitClicked() {
    QString msg = m_commitInput->text().trimmed();
    if (msg.isEmpty()) {
        m_commitInput->setPlaceholderText("Cannot commit with empty message!");
        QString errCol = m_dark ? "#e06c75" : "#d32f2f";
        m_commitInput->setStyleSheet(QString("border: 1px solid %1; font-size: 11px; padding: 4px 6px;").arg(errCol));
        QTimer::singleShot(2000, this, [this]() {
            m_commitInput->setPlaceholderText("Commit message...");
            m_commitInput->setStyleSheet("font-size: 11px; padding: 4px 6px;");
        });
        return;
    }
    setBusy(true);
    m_pendingCommit = m_executor->execute("git", {"commit", "-m", msg}, m_repoPath, 30000);
    connect(m_executor, &TerminalExecutor::finished, this, [this](int id, int exitCode, const QString &output) {
        if (id == m_pendingCommit) {
            onCommitDone(id, exitCode, output);
        }
    });
    m_commitInput->clear();
    emit gitOutput(QString("[Git] Committing: %1").arg(msg));
}

void GitPanel::onCommitDone(int id, int exitCode, const QString &output) {
    if (id != m_pendingCommit) return;
    m_pendingCommit = -1;

    if (exitCode == 0) {
        emit gitOutput("[Git] Commit successful");
        refreshStatus();
        refreshLog();
    } else {
        emit gitOutput(QString("[Git] Commit failed: %1").arg(output.trimmed()));
    }
    setBusy(false);
}

void GitPanel::onPushClicked() {
    setBusy(true);
    m_pendingPush = m_executor->execute("git", {"push", "origin"}, m_repoPath, 60000);
    connect(m_executor, &TerminalExecutor::finished, this, [this](int id, int exitCode, const QString &output) {
        if (id == m_pendingPush) {
            onPushDone(id, exitCode, output);
        }
    });
    emit gitOutput("[Git] Pushing to origin...");
}

void GitPanel::onPushDone(int id, int exitCode, const QString &output) {
    if (id != m_pendingPush) return;
    m_pendingPush = -1;

    if (exitCode == 0) {
        emit gitOutput("[Git] Push successful");
        refreshStatus();
        refreshLog();
    } else {
        emit gitOutput(QString("[Git] Push failed: %1").arg(output.trimmed()));
    }
    setBusy(false);
}

void GitPanel::onPullClicked() {
    setBusy(true);
    m_pendingPull = m_executor->execute("git", {"pull", "origin"}, m_repoPath, 60000);
    connect(m_executor, &TerminalExecutor::finished, this, [this](int id, int exitCode, const QString &output) {
        if (id == m_pendingPull) {
            onPullDone(id, exitCode, output);
        }
    });
    emit gitOutput("[Git] Pulling from origin...");
}

void GitPanel::onPullDone(int id, int exitCode, const QString &output) {
    if (id != m_pendingPull) return;
    m_pendingPull = -1;

    if (exitCode == 0) {
        emit gitOutput("[Git] Pull successful");
        refreshStatus();
        refreshLog();
        refreshBranches();
    } else {
        emit gitOutput(QString("[Git] Pull failed: %1").arg(output.trimmed()));
    }
    setBusy(false);
}

// ===== Branches ==========================================

void GitPanel::refreshBranches() {
    if (m_repoPath.isEmpty()) return;
    if (m_pendingBranch >= 0) return;
    m_pendingBranch = m_executor->execute(
        "git", {"branch", "--all"}, m_repoPath, 10000);
    connect(m_executor, &TerminalExecutor::finished, this, [this](int id, int exitCode, const QString &output) {
        if (id == m_pendingBranch) {
            onBranchDone(id, exitCode, output);
        }
    });
}

void GitPanel::onBranchDone(int id, int exitCode, const QString &output) {
    if (id != m_pendingBranch) return;
    m_pendingBranch = -1;

    if (exitCode != 0) return;

    QString currentBranch;
    m_branchCombo->blockSignals(true);
    m_branchCombo->clear();

    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        QString branch = line.trimmed();
        if (branch.startsWith('*')) {
            currentBranch = branch.mid(1).trimmed();
            m_branchCombo->addItem("\u2713 " + currentBranch);
        } else {
            m_branchCombo->addItem("  " + branch);
        }
    }

    if (!currentBranch.isEmpty()) {
        m_branchLabel->setText(currentBranch);
    }

    m_branchCombo->blockSignals(false);
}

void GitPanel::onBranchSwitched(int index) {
    if (index < 0) return;
    QString text = m_branchCombo->currentText().trimmed();
    if (text.startsWith("\u2713")) return;

    QString branchName = text.mid(2).trimmed();
    if (branchName.isEmpty()) return;

    setBusy(true);
    runGit({"checkout", branchName});
    emit gitOutput(QString("[Git] Switched to branch: %1").arg(branchName));
    QTimer::singleShot(1500, this, [this]() {
        refresh();
        setBusy(false);
    });
}

void GitPanel::onCreateBranch() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Branch",
                                          "Enter branch name:", QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    setBusy(true);
    runGit({"checkout", "-b", name.trimmed()});
    emit gitOutput(QString("[Git] Created and switched to branch: %1").arg(name));
    QTimer::singleShot(1500, this, [this]() {
        refresh();
        setBusy(false);
    });
}

// ===== Log ===============================================

void GitPanel::refreshLog() {
    if (m_repoPath.isEmpty()) return;
    if (m_pendingLog >= 0) return;
    m_pendingLog = m_executor->execute(
        "git", {"log", "--oneline", "-20", "--decorate", "--all"}, m_repoPath, 10000);
    connect(m_executor, &TerminalExecutor::finished, this, [this](int id, int exitCode, const QString &output) {
        if (id == m_pendingLog) {
            onLogDone(id, exitCode, output);
        }
    });
}

void GitPanel::onLogDone(int id, int exitCode, const QString &output) {
    if (id != m_pendingLog) return;
    m_pendingLog = -1;
    m_logList->clear();

    if (exitCode != 0) {
        m_logLabel->setText("\U0001F4CB  Commit History (error)");
        return;
    }

    QStringList lines = output.split('\n', Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        auto *item = new QListWidgetItem("  " + line.trimmed());
        item->setToolTip(QString("Commit: %1\nDouble-click to view full diff.").arg(line.section(' ', 0, 0)));
        QString headColor = m_dark ? "#98c379" : "#2e7d32";
        QString logColor  = m_dark ? "#abb2bf" : "#333333";
        if (line.contains("HEAD")) {
            item->setForeground(QColor(headColor));
        } else {
            item->setForeground(QColor(logColor));
        }
        m_logList->addItem(item);
    }

    m_logLabel->setText(QString("\U0001F4CB  Commit History  [%1 commits]").arg(lines.size()));
}

// ===== File tree ==========================================

void GitPanel::onFileTreeClicked(const QModelIndex &index) {
    if (!index.isValid()) return;
    QFileInfo fi = m_fileSystemModel->fileInfo(index);
    if (fi.isFile()) {
        emit diffRequested(fi.absoluteFilePath());
    }
}