#include "MainWindow.h"
#include "orchestrator.h"
#include "api_client.h"
#include "process_manager.h"
#include "vector_db.h"
#include "logger.h"

// هدرهای ضروری برای ابزارهای جانبی
#include "terminal_executor.h"
#include "git_manager.h"
#include "build_pipeline.h"
#include "settings_manager.h"
#include "vram_manager.h"
#include "system_tray.h"
#include "session_manager.h"
//#include "project_explorer.h"
#include "monitor_widget.h"
#include "git_panel.h"
//#include "code_editor.h"
//#include "chat_editor.h"
#include "settings_dialog.h"

#include <QApplication>
#include <QStyleFactory>
#include <QScrollArea>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollBar>
#include <QTimer>
#include <QFileInfo>
#include <QRadioButton>
#include <QButtonGroup>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QStackedWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QMenuBar>
#include <QMenu>
#include <QFileSystemModel>
#include <QTreeView>
#include <QProcess>
#include <QRegularExpression>
#include <QShowEvent> // ضروری برای رفع خطای لینکر


MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    LOG_INFO("MainWindow", "Initializing Qt AI Agent Orchestrator");

    m_processManager = new ProcessManager(this);
    m_apiClient = new ApiClient(this);
    m_orchestrator = new Orchestrator(m_apiClient, m_processManager, this);

    m_terminalExecutor = new TerminalExecutor(this);
    m_gitManager = new GitManager(m_terminalExecutor, this);
    m_buildPipeline = new BuildPipeline(m_terminalExecutor, m_apiClient, this);
    m_settingsManager = new SettingsManager(this);
    m_vramManager = new VramManager(m_processManager, this);
    m_vramManager->setTotalVRAM(m_settingsManager->vramLimitMB());
    m_systemTray = new SystemTray(this);
    m_sessionManager = new SessionManager(this);

    m_imagePreview = nullptr;
    m_vramProcess = nullptr;

    setupUI();
    m_isDark = m_settingsManager->theme() == "dark";
    applyTheme();

    // Auto-detect repo and show in GitPanel after 2s
    QTimer::singleShot(2000, this, [this]() {
        QString repoPath = m_currentBuildDir.isEmpty() ?
                               QApplication::applicationDirPath() : m_currentBuildDir;
        m_gitPanel->setRepoPath(repoPath);
        m_gitManager->setRepoPath(repoPath);
    });

    connect(m_processManager, &ProcessManager::modelStarted, this, &MainWindow::onModelStarted);
    connect(m_processManager, &ProcessManager::modelStopped, this, &MainWindow::onModelStopped);
    connect(m_processManager, &ProcessManager::modelOutput, this, [this](int port, const QString &output) {
        LOG_INFO(m_portToKey.value(port, QString("Model[%1]").arg(port)), output);
    });
    connect(m_processManager, &ProcessManager::modelError, this, [this](int port, const QString &err) {
        if (m_modelLogDisplay) {
            QString failColor = m_isDark ? "#e06c75" : "#c62828";
            m_modelLogDisplay->append(QString("<span style='color:%1; font-weight: bold;'>[FAIL] %2</span>")
                                          .arg(failColor).arg(err.toHtmlEscaped()));
        }
        // Reset VRAM manager state so the user can retry
        if (m_portToKey.contains(port)) {
            m_failedModels << m_portToKey[port];
            m_vramManager->releaseModel(m_portToKey[port]);
        }
        updateModelStatusUI();
        advanceStartAllQueue(port);
    });

    // PDF processor signals (via Orchestrator)
    connect(m_orchestrator, &Orchestrator::pdfProcessingStarted, this, [this](const QString &filename) {
        appendChat("System", QString("Processing PDF: %1...").arg(filename), "#e5c07b");
        if (m_modelLogDisplay)
            m_modelLogDisplay->append(QString("<span style='color:#333333;'>[PDF] Starting processing: %1</span>").arg(filename.toHtmlEscaped()));
        m_pdfBtn->setEnabled(false);
        m_pdfBtn->setText("⏳");
    });
    connect(m_orchestrator, &Orchestrator::pdfProcessingProgress, this, [this](const QString &filename, int page, int totalPages) {
        Q_UNUSED(filename)
        if (m_modelLogDisplay)
            m_modelLogDisplay->append(QString("<span style='color:#333333;'>[PDF] Processing page %1/%2</span>").arg(page).arg(totalPages));
    });
    connect(m_orchestrator, &Orchestrator::pdfProcessed, this, [this](const QString &filename, int chunks, int pages) {
        appendChat("System", QString("PDF ingested successfully: %1\nPages: %2 | Chunks: %3").arg(filename).arg(pages).arg(chunks), "#98c379");
        if (m_modelLogDisplay)
            m_modelLogDisplay->append(QString("<span style='color:#333333;'>[PDF] Completed: %1 — %2 chunks stored</span>").arg(filename.toHtmlEscaped()).arg(chunks));
        refreshPdfList();
        m_pdfBtn->setEnabled(true);
        m_pdfBtn->setText("📄");
    });
    connect(m_orchestrator, &Orchestrator::pdfError, this, [this](const QString &filename, const QString &err) {
        appendChat("System", QString("PDF processing failed: %1\nError: %2").arg(filename, err), "#e06c75");
        LOG_ERROR("PDF", QString("%1: %2").arg(filename, err));
        if (m_modelLogDisplay)
            m_modelLogDisplay->append(QString("<span style='color:#e06c75; font-weight: bold;'>[PDF] Error: %1 — %2</span>").arg(filename.toHtmlEscaped(), err.toHtmlEscaped()));
        m_pdfBtn->setEnabled(true);
        m_pdfBtn->setText("📄");
    });
    connect(m_orchestrator, &Orchestrator::pdfVerified, this, [this](const QString &filename, bool success, const QString &message) {
        if (m_modelLogDisplay) {
            if (success) {
                m_modelLogDisplay->append(QString("<span style='color:#333333;'>[PDF] Verified: %1 — %2</span>").arg(filename.toHtmlEscaped(), message.toHtmlEscaped()));
            } else {
                m_modelLogDisplay->append(QString("<span style='color:#e06c75; font-weight: bold;'>[PDF] Verification failed: %1 — %2</span>").arg(filename.toHtmlEscaped(), message.toHtmlEscaped()));
            }
        }
    });
    connect(m_orchestrator, &Orchestrator::pdfError, this, [this](const QString &filename, const QString &err) {
        appendChat("System", QString("PDF processing failed: %1\nError: %2").arg(filename, err), "#e06c75");
        LOG_ERROR("PDF", QString("%1: %2").arg(filename, err));
        m_pdfBtn->setEnabled(true);
        m_pdfBtn->setText("📄");
    });

    // Register models from settings.json
    {
        QString serverPath = m_settingsManager->llamaServerPath();
        QString modelsDir = m_settingsManager->modelsDirectory();
        for (const QString &key : m_settingsManager->modelKeys()) {
            auto cfg = m_settingsManager->modelConfig(key);
            QString modelPath = modelsDir + "/" + cfg.modelFile;
            QString mmprojPath = cfg.mmprojFile.isEmpty() ? "" : modelsDir + "/" + cfg.mmprojFile;
            ModelInfo info;
            info.name = cfg.name;
            info.serverPath = serverPath;
            info.modelPath = modelPath;
            info.mmprojPath = mmprojPath;
            info.port = cfg.port;
            info.ngl = cfg.ngl;
            info.ctx = cfg.ctx;
            info.isVision = cfg.isVision;
            info.isEmbedding = cfg.isEmbedding;
            info.estimatedVRAM_MB = cfg.vramMB;
            info.schedule = "on-demand";
            info.batchSize = cfg.batchSize;
            info.ubatchSize = cfg.ubatchSize;
            info.temperature = cfg.temperature;
            m_vramManager->registerModel(key, info);
            m_portToKey[cfg.port] = key;
            LOG_INFO("MainWindow", QString("Registered %1: %2").arg(key, cfg.modelFile));
        }
    }

    // VRAM manager model lifecycle → Model Logs tab
    connect(m_vramManager, &VramManager::modelLoading, this, [this](const QString &key) {
        refreshVRAMAsync();
        if (m_monitorWidget)
            m_monitorWidget->refreshGPU();
        if (m_modelLogDisplay) {
            QString c = m_isDark ? "#e5c07b" : "#e65100";
            m_modelLogDisplay->append(QString("<span style='color:%1; font-weight: bold;'>[VRAM] Loading <b>%2</b>...</span>")
                                          .arg(c, key.toHtmlEscaped()));
        }
    });
    connect(m_vramManager, &VramManager::modelLoaded, this, [this](const QString &key) {
        refreshVRAMAsync();
        if (m_monitorWidget)
            m_monitorWidget->refreshGPU();
        if (m_modelLogDisplay) {
            QString c = m_isDark ? "#98c379" : "#2e7d32";
            m_modelLogDisplay->append(QString("<span style='color:%1; font-weight: bold;'>[VRAM] <b>%2</b> loaded → VRAM</span>")
                                          .arg(c, key.toHtmlEscaped()));
        }
    });
    connect(m_vramManager, &VramManager::modelUnloaded, this, [this](const QString &key) {
        refreshVRAMAsync();
        if (m_monitorWidget)
            m_monitorWidget->refreshGPU();
        if (m_modelLogDisplay) {
            QString c = m_isDark ? "#e5c07b" : "#e65100";
            m_modelLogDisplay->append(QString("<span style='color:%1; font-weight: bold;'>[VRAM] <b>%2</b> unloaded from VRAM</span>")
                                          .arg(c, key.toHtmlEscaped()));
        }
    });

    // Connect VRAM manager to orchestrator for model swapping
    connect(m_orchestrator, &Orchestrator::modelSwapRequested, this, [this](int port) {
        QString scenario;
        if (port == m_settingsManager->modelPort("coder")) scenario = "code";
        else if (port == m_settingsManager->modelPort("expert")) scenario = "deep";
        else if (port == m_settingsManager->modelPort("vision")) scenario = "vision";
        else scenario = "plan";
        m_vramManager->ensureModelForScenario(scenario);
    });

    // Connect build pipeline
    connect(m_buildPipeline, &BuildPipeline::buildDone, this, &MainWindow::onBuildDone);
    connect(m_buildPipeline, &BuildPipeline::buildProgress, this,
            [this](int id, const QString &output) {
                Q_UNUSED(id)
                if (m_terminalOutput)
                    m_terminalOutput->append(output.trimmed());
            });
    connect(m_buildPipeline, &BuildPipeline::buildError, this,
            [this](int id, const QString &err) {
                Q_UNUSED(id)
                if (m_terminalOutput)
                    m_terminalOutput->append("<span style='color:#e06c75;'>" + err.toHtmlEscaped() + "</span>");
            });

    // Wire terminal executor output to terminal tab
    connect(m_terminalExecutor, &TerminalExecutor::outputReady, this,
            [this](int id, const QString &output) {
                Q_UNUSED(id)
                if (m_terminalOutput)
                    m_terminalOutput->append(output.trimmed());
            });
    connect(m_terminalExecutor, &TerminalExecutor::errorOutput, this,
            [this](int id, const QString &output) {
                Q_UNUSED(id)
                if (m_terminalOutput)
                    m_terminalOutput->append("<span style='color:#e06c75;'>" + output.toHtmlEscaped() + "</span>");
            });
    connect(m_terminalExecutor, &TerminalExecutor::finished, this,
            [this](int id, int exitCode, const QString &) {
                Q_UNUSED(id)
                if (m_terminalOutput) {
                    QString color = exitCode == 0 ? "#98c379" : "#e06c75";
                    m_terminalOutput->append(QString("<span style='color:%1;'>Process exited with code %2</span>")
                                                 .arg(color).arg(exitCode));
                }
                m_terminalSendBtn->setEnabled(true);
            });

    // Orchestrator signals
    connect(m_orchestrator, &Orchestrator::chatResponse, this, &MainWindow::onOrchestratorResponse);
    connect(m_orchestrator, &Orchestrator::planResponse, this, &MainWindow::onOrchestratorPlan);
    connect(m_orchestrator, &Orchestrator::agentResponse, this, &MainWindow::onOrchestratorAgent);
    connect(m_orchestrator, &Orchestrator::pipelineLog, this, &MainWindow::onOrchestratorLog);
    connect(m_orchestrator, &Orchestrator::pipelineError, this, &MainWindow::onOrchestratorError);
    connect(m_orchestrator, &Orchestrator::pipelineStage, this, &MainWindow::onAgentStageUpdate);
    connect(m_orchestrator, &Orchestrator::patchApprovalRequested, this, &MainWindow::onPatchApprovalRequested);
    connect(m_orchestrator, &Orchestrator::processingStarted, this, [this]() {
        m_sendBtn->setEnabled(false);
        m_sendBtn->setText("...");
        m_pdfBtn->setEnabled(false);
        m_pdfBtn->setText("⏳");
    });
    connect(m_orchestrator, &Orchestrator::processingFinished, this, [this]() {
        m_sendBtn->setEnabled(true);
        m_sendBtn->setText("Send");
        m_pdfBtn->setEnabled(true);
        m_pdfBtn->setText("📄");
    });

    connect(Logger::instance(), &Logger::logEmitted, this,
            [this](const QString &formatted, LogLevel level, const QString &) {
                if (level == LogLevel::Error) {
                    QString c = m_isDark ? "#e06c75" : "#c62828";
                    m_modelLogDisplay->append("<span style='color:" + c + "; font-weight: bold;'>" + formatted.toHtmlEscaped() + "</span>");
                } else if (level == LogLevel::Warning) {
                    QString c = m_isDark ? "#e5c07b" : "#e65100";
                    m_modelLogDisplay->append("<span style='color:" + c + "; font-weight: bold;'>" + formatted.toHtmlEscaped() + "</span>");
                } else {
                    QString c = m_isDark ? "#abb2bf" : "#333333";
                    m_modelLogDisplay->append("<span style='color:" + c + ";'>" + formatted.toHtmlEscaped() + "</span>");
                }
            });

    initTray();
    initSessionPersistence();

    // Restore geometry
    QByteArray geo = m_settingsManager->windowGeometry();
    if (!geo.isEmpty()) restoreGeometry(geo);
    QByteArray state = m_settingsManager->windowState();
    if (!state.isEmpty()) restoreState(state);

    // Session auto-save every 5 minutes
    auto *sessionTimer = new QTimer(this);
    connect(sessionTimer, &QTimer::timeout, this, &MainWindow::saveSession);
    sessionTimer->start(300000);

    auto *autoSaveTimer = new QTimer(this);
    connect(autoSaveTimer, &QTimer::timeout, this, &MainWindow::onAutoSaveCode);
    autoSaveTimer->start(30000);

    m_healthTimer = new QTimer(this);
    connect(m_healthTimer, &QTimer::timeout, this, [this]() {
        auto checkPort = [this](int port, const QString &name) {
            m_apiClient->checkHealth(port, [this, port, name](bool alive, int ms) {
                QString status = alive ? "<span style='color:#98c379;'>● UP</span>" :
                                     "<span style='color:#e06c75;'>○ DOWN</span>";
                LOG_INFO("Health", QString("%1: %2 (%3ms)").arg(name).arg(alive ? "UP" : "DOWN").arg(ms));
            });
        };
        auto tryCheck = [this, &checkPort](const QString &key, const QString &displayName) {
            int port = m_settingsManager->modelPort(key);
            if (m_processManager->isRunning(port))
                checkPort(port, displayName);
        };
        tryCheck("planner", "Planner");
        tryCheck("embed", "Embed");
        tryCheck("coder", "Coder");
        tryCheck("expert", "Expert");
        tryCheck("vision", "Vision");
    });
    m_healthTimer->start(30000);

    // Monitoring timer
    m_monitorTimer = new QTimer(this);
    connect(m_monitorTimer, &QTimer::timeout, this, &MainWindow::onMonitorUpdate);
    m_monitorTimer->start(3000);

    // Models are registered but NOT started — user clicks Start
    updateModelStatusUI();
    refreshVRAMAsync();
    if (m_monitorWidget)
        m_monitorWidget->refreshGPU();
    refreshPdfList();

    // Auto-start models if configured
    if (m_settingsManager->autoStartModels()) {
        LOG_INFO("MainWindow", "Auto-starting models...");
        QTimer::singleShot(500, this, &MainWindow::onStartAllModels);
    }

    LOG_INFO("MainWindow", "Ready. Waiting for user input.");
}

MainWindow::~MainWindow() {
    m_healthTimer->stop();
    m_monitorTimer->stop();
    saveSession();
    m_settingsManager->setWindowGeometry(saveGeometry());
    m_settingsManager->setWindowState(saveState());
    m_settingsManager->setMainSplitterPos(m_mainSplitter->sizes().value(0, 220));
    m_settingsManager->save();
    m_processManager->terminateAll();
}

ModelConfig MainWindow::modelConfigFromSettings(const QString &key) const {
    auto cfg = m_settingsManager->modelConfig(key);
    QString modelPath = m_settingsManager->modelsDirectory() + "/" + cfg.modelFile;
    QString mmprojPath;
    if (!cfg.mmprojFile.isEmpty())
        mmprojPath = m_settingsManager->modelsDirectory() + "/" + cfg.mmprojFile;
    return { cfg.name, modelPath, mmprojPath, cfg.port, cfg.ctx, cfg.ngl,
            cfg.isEmbedding, false, cfg.isVision,
            cfg.batchSize, cfg.ubatchSize, cfg.temperature };
}

int MainWindow::modelPort(const QString &key) const {
    return m_settingsManager->modelPort(key);
}

void MainWindow::startAllModels() {
    LOG_INFO("System", "Starting all models (planner, embed, coder)...");
    m_startAllBtn->setEnabled(false);
    m_startAllModelsAction->setEnabled(false);
    m_failedModels.clear();
    m_startAllQueue = QStringList() << "planner" << "embed" << "coder";
    m_startAllIndex = 0;
    startNextInQueue();
}

void MainWindow::startNextInQueue() {
    if (m_startAllIndex < 0 || m_startAllIndex >= m_startAllQueue.size()) {
        m_startAllBtn->setEnabled(true);
        m_startAllModelsAction->setEnabled(true);
        updateModelStatusUI();
        return;
    }
    const QString &key = m_startAllQueue[m_startAllIndex];
    LOG_INFO("System", QString("Starting %1...").arg(key));
    m_vramManager->ensureModel(key);
    updateModelStatusUI();
}

void MainWindow::advanceStartAllQueue(int port) {
    if (m_startAllIndex < 0 || m_startAllIndex >= m_startAllQueue.size())
        return;
    if (m_portToKey.value(port) != m_startAllQueue[m_startAllIndex])
        return;
    LOG_INFO("System", QString("%1 finished, advancing to next model").arg(m_startAllQueue[m_startAllIndex]));
    m_startAllIndex++;
    startNextInQueue();
}

void MainWindow::stopAllModels() {
    LOG_INFO("System", "Stopping all models...");
    m_vramManager->releaseAll();
    m_processManager->terminateAll();
    m_failedModels.clear();
    m_startAllIndex = -1;
    m_startAllQueue.clear();
    m_startAllBtn->setEnabled(true);
    m_startAllModelsAction->setEnabled(true);
    updateModelStatusUI();
    if (m_modelLogDisplay) {
        QString stopColor = m_isDark ? "#e5c07b" : "#e65100";
        m_modelLogDisplay->append(QString("<span style='color:%1; font-weight: bold;'>[STOP] All models stopped</span>")
                                      .arg(stopColor));
    }
}

void MainWindow::startPlanner() {
    LOG_INFO("System", "Starting Planner model...");
    m_failedModels.removeAll("planner");
    m_vramManager->ensureModel("planner");
    updateModelStatusUI();
}

void MainWindow::startEmbed() {
    LOG_INFO("System", "Starting Embed model...");
    m_failedModels.removeAll("embed");
    m_vramManager->ensureModel("embed");
    updateModelStatusUI();
}

void MainWindow::startCoder() {
    LOG_INFO("System", "Starting Coder model...");
    m_failedModels.removeAll("coder");
    m_vramManager->ensureModel("coder");
    updateModelStatusUI();
}

void MainWindow::startExpert() {
    LOG_INFO("System", "Starting Expert model...");
    m_failedModels.removeAll("expert");
    m_vramManager->ensureModel("expert");
    updateModelStatusUI();
}

void MainWindow::startVision() {
    LOG_INFO("System", "Starting Vision model...");
    m_failedModels.removeAll("vision");
    m_vramManager->ensureModel("vision");
    updateModelStatusUI();
}

void MainWindow::updateModelStatusUI() {
    QString onColor      = m_isDark ? "#98c379" : "#2e7d32";
    QString loadingColor = m_isDark ? "#e5c07b" : "#e65100";
    QString failColor    = m_isDark ? "#e06c75" : "#c62828";

    for (auto it = m_modelStatusDots.begin(); it != m_modelStatusDots.end(); ++it) {
        const QString &key = it.key();
        QLabel *dot = it.value();

        int port = m_settingsManager->modelPort(key);
        bool running = m_processManager->isRunning(port);
        bool loading = m_vramManager->isModelLoaded(key) && !running;

        QString color = running ? onColor : (loading ? loadingColor : failColor);
        dot->setStyleSheet(QString("font-size: 14px; color: %1;").arg(color));
    }
}

// ── UI Setup ──────────────────────────────────────────────

void MainWindow::setupUI() {
    auto *centralWidget = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(centralWidget);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    setCentralWidget(centralWidget);

    createMenuBar();

    createTopBar(mainLayout);

    m_mainSplitter = new QSplitter(Qt::Horizontal);
    m_mainSplitter->setHandleWidth(1);

    createLeftPanel(m_mainSplitter);
    createCenterPanel(m_mainSplitter);

    m_mainSplitter->setStretchFactor(0, 1);
    m_mainSplitter->setStretchFactor(1, 4);
    mainLayout->addWidget(m_mainSplitter, 1);
}

void MainWindow::createMenuBar() {
    auto *mb = menuBar();
    // Colors handled by applyTheme() — keep only structural styles here
    mb->setStyleSheet(
        "QMenuBar { padding: 2px 0; font-size: 12px; }"
        "QMenuBar::item { padding: 4px 10px; }"
        "QMenu { padding: 4px; }"
        "QMenu::item { padding: 6px 24px; }"
        "QMenu::separator { height: 1px; margin: 4px 8px; }"
        );

    auto *fileMenu = mb->addMenu("File");
    auto *openAction = fileMenu->addAction("Open File...");
    openAction->setShortcut(QKeySequence("Ctrl+O"));
    connect(openAction, &QAction::triggered, this, [this]() {
        QFileDialog dlg(this, "Open File");
        dlg.setOption(QFileDialog::DontUseNativeDialog);
        dlg.setFileMode(QFileDialog::ExistingFile);
        if (dlg.exec() == QDialog::Accepted) {
            QString path = dlg.selectedFiles().first();
            m_currentBuildDir = QFileInfo(path).absolutePath();
            m_terminalCwdLabel->setText(m_currentBuildDir);
            m_gitPanel->setRepoPath(m_currentBuildDir);
            m_gitManager->setRepoPath(m_currentBuildDir);
            LOG_INFO("File", "Opened: " + path);
            m_terminalOutput->append(
                QString("<span style='color:#61afef;'>[File] Opened: %1</span>")
                    .arg(path.toHtmlEscaped()));
        }
    });
    auto *openFolderAction = fileMenu->addAction("Open Folder...");
    openFolderAction->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(openFolderAction, &QAction::triggered, this, &MainWindow::onOpenFolder);
    fileMenu->addSeparator();
    auto *saveAction = fileMenu->addAction("Save");
    saveAction->setShortcut(QKeySequence::Save);
    connect(saveAction, &QAction::triggered, this, &MainWindow::onSaveCurrentFile);
    auto *saveAsAction = fileMenu->addAction("Save As...");
    saveAsAction->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(saveAsAction, &QAction::triggered, this, &MainWindow::onSaveCurrentFileAs);
    fileMenu->addSeparator();
    auto *exitAction = fileMenu->addAction("Exit");
    exitAction->setShortcut(QKeySequence::Quit);
    connect(exitAction, &QAction::triggered, this, &QWidget::close);

    auto *modelsMenu = mb->addMenu("Models");
    m_startAllModelsAction = modelsMenu->addAction("▶ Start All Models");
    m_startAllModelsAction->setShortcut(QKeySequence("Ctrl+Shift+M"));
    connect(m_startAllModelsAction, &QAction::triggered, this, &MainWindow::onStartAllModels);
    modelsMenu->addSeparator();
    auto *startPlanner = modelsMenu->addAction("Start Planner");
    connect(startPlanner, &QAction::triggered, this, &MainWindow::startPlanner);
    auto *startEmbed = modelsMenu->addAction("Start Embed");
    connect(startEmbed, &QAction::triggered, this, &MainWindow::startEmbed);
    auto *startCoder = modelsMenu->addAction("Start Coder");
    connect(startCoder, &QAction::triggered, this, &MainWindow::startCoder);
    auto *startExpert = modelsMenu->addAction("Start Expert");
    connect(startExpert, &QAction::triggered, this, &MainWindow::startExpert);
    auto *startVision = modelsMenu->addAction("Start Vision");
    connect(startVision, &QAction::triggered, this, &MainWindow::startVision);
    modelsMenu->addSeparator();
    auto *stopAll = modelsMenu->addAction("Stop All Models");
    connect(stopAll, &QAction::triggered, this, [this]() {
        m_processManager->terminateAll();
        updateModelStatusUI();
    });

    auto *viewMenu = mb->addMenu("View");
    auto *toggleLeft = viewMenu->addAction("Toggle Left Panel");
    toggleLeft->setCheckable(true);
    toggleLeft->setChecked(true);
    connect(toggleLeft, &QAction::toggled, this, [this](bool checked) {
        m_leftTabs->setVisible(checked);
    });

    createRunMenu(mb);

    auto *sessionMenu = mb->addMenu("Session");
    auto *saveSessionAction = sessionMenu->addAction("Save Session");
    saveSessionAction->setShortcut(QKeySequence("Ctrl+S"));
    connect(saveSessionAction, &QAction::triggered, this, &MainWindow::onSaveSession);

    auto *loadSessionAction = sessionMenu->addAction("Load Session");
    loadSessionAction->setShortcut(QKeySequence("Ctrl+Shift+L"));
    connect(loadSessionAction, &QAction::triggered, this, &MainWindow::onLoadSession);

    auto *settingsAction = mb->addAction("Settings");
    settingsAction->setShortcut(QKeySequence("Ctrl+,"));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);

    auto *helpMenu = mb->addMenu("Help");
    helpMenu->addAction("About", this, [this]() {
        QString modelsInfo;
        for (const QString &key : m_settingsManager->modelKeys()) {
            auto cfg = m_settingsManager->modelConfig(key);
            int port = cfg.port;
            bool running = m_processManager->isRunning(port);
            QString modelName = cfg.name.isEmpty() ? key : cfg.name;
            modelsInfo += QString("%1 %2 (port %3, %4 ctx, %5 ngl)\n")
                              .arg(running ? "●" : "○")
                              .arg(modelName, QString::number(port))
                              .arg(cfg.ctx).arg(cfg.ngl);
        }
        QMessageBox::about(this, "About Qt AI Agent Orchestrator",
                           "Qt AI Agent Orchestrator v1.0\n\n"
                           "Registered Models:\n" + modelsInfo + "\n"
                                              "Backend: llama.cpp\n"
                                              "Framework: Qt6 / C++17");
    });
}

void MainWindow::createRunMenu(QMenuBar *mb) {
    auto *runMenu = mb->addMenu("Run");

    auto *buildAction = runMenu->addAction("Build Project");
    buildAction->setShortcut(QKeySequence("Ctrl+B"));
    buildAction->setIcon(QIcon());
    connect(buildAction, &QAction::triggered, this, [this]() {
        onBuildClicked();
    });

    auto *rebuildAction = runMenu->addAction("Rebuild (Clean + Build)");
    connect(rebuildAction, &QAction::triggered, this, [this]() {
        m_buildPipeline->setBuildCommand("make", {"clean"});
        m_buildPipeline->build();
        m_buildPipeline->setBuildCommand("make");
        m_buildPipeline->build();
    });

    runMenu->addSeparator();

    auto *runAction = runMenu->addAction("Run Project");
    runAction->setShortcut(QKeySequence("Ctrl+R"));
    connect(runAction, &QAction::triggered, this, [this]() {
        onRunClicked();
    });

    runMenu->addSeparator();

    auto *gitMenu = runMenu->addMenu("Git");

    auto *gitStatus = gitMenu->addAction("Status");
    connect(gitStatus, &QAction::triggered, this, [this]() {
        m_gitManager->setRepoPath(m_currentBuildDir.isEmpty() ?
                                      QApplication::applicationDirPath() : m_currentBuildDir);
        m_gitManager->status();
    });

    auto *gitDiff = gitMenu->addAction("Diff");
    connect(gitDiff, &QAction::triggered, this, [this]() {
        m_gitManager->setRepoPath(m_currentBuildDir.isEmpty() ?
                                      QApplication::applicationDirPath() : m_currentBuildDir);
        m_gitManager->diff();
    });

    auto *gitLog = gitMenu->addAction("Log");
    connect(gitLog, &QAction::triggered, this, [this]() {
        m_gitManager->setRepoPath(m_currentBuildDir.isEmpty() ?
                                      QApplication::applicationDirPath() : m_currentBuildDir);
        m_gitManager->log();
    });

    auto *gitCommit = gitMenu->addAction("Quick Commit...");
    connect(gitCommit, &QAction::triggered, this, [this]() {
        m_gitManager->setRepoPath(m_currentBuildDir.isEmpty() ?
                                      QApplication::applicationDirPath() : m_currentBuildDir);
        m_gitManager->addAll();
        m_gitManager->commit("Quick commit");
    });

    runMenu->addSeparator();

    auto *stopAction = runMenu->addAction("Stop");
    stopAction->setShortcut(QKeySequence("Ctrl+Shift+X"));
    connect(stopAction, &QAction::triggered, this, [this]() {
        m_terminalExecutor->cancelAll();
        m_terminalOutput->append("<span style='color:#e5c07b;'>[STOP] All processes cancelled.</span>");
    });
}

void MainWindow::createTopBar(QVBoxLayout *mainLayout) {
    m_topBar = new QWidget();
    m_topBar->setFixedHeight(36);
    auto *topLayout = new QHBoxLayout(m_topBar);
    topLayout->setContentsMargins(12, 0, 12, 0);

    m_appIcon = new QLabel("◆ AI Orchestrator");
    m_appIcon->setStyleSheet("font-weight: bold; font-size: 15px;");

    m_modeGroup = new QButtonGroup(this);
    auto *chatMode = new QRadioButton("Chat");
    auto *planMode = new QRadioButton("Plan");
    auto *agentMode = new QRadioButton("Agent");
    chatMode->setChecked(true);

    QString modeBtnStyle =
        "QRadioButton { font-size: 11px; padding: 2px 10px; "
        "border: 1px solid; border-radius: 3px; }"
        "QRadioButton::indicator { width: 0; height: 0; }"
        "QRadioButton:checked { border-color: #6c63ff; }";
    chatMode->setStyleSheet(modeBtnStyle);
    planMode->setStyleSheet(modeBtnStyle);
    agentMode->setStyleSheet(modeBtnStyle);

    m_modeGroup->addButton(chatMode, 0);
    m_modeGroup->addButton(planMode, 1);
    m_modeGroup->addButton(agentMode, 2);
    connect(m_modeGroup, &QButtonGroup::idClicked, this, &MainWindow::onModeChanged);

    m_themeBtn = new QPushButton("☀");
    m_themeBtn->setFixedSize(28, 28);
    m_themeBtn->setToolTip("Toggle theme");
    m_themeBtn->setStyleSheet(
        "QPushButton { background-color: transparent; border: none; "
        "font-size: 16px; border-radius: 4px; }"
        );
    connect(m_themeBtn, &QPushButton::clicked, this, [this]() {
        m_isDark = !m_isDark;
        applyTheme();
        m_settingsManager->setTheme(m_isDark ? "dark" : "light");
        m_settingsManager->save();
    });

    m_vramLabel = new QLabel("VRAM: --");
    m_vramLabel->setStyleSheet("color: #98c379; font-size: 13px; padding: 0 8px;");
    // keep VRAM label green in both themes

    topLayout->addWidget(m_appIcon);
    topLayout->addSpacing(12);
    topLayout->addWidget(chatMode);
    topLayout->addWidget(planMode);
    topLayout->addWidget(agentMode);
    topLayout->addStretch();
    topLayout->addWidget(m_themeBtn);
    topLayout->addWidget(m_vramLabel);
    mainLayout->addWidget(m_topBar);
}

void MainWindow::createLeftPanel(QSplitter *mainSplitter) {
    m_leftTabs = new QTabWidget();
    m_leftTabs->setDocumentMode(true);
    m_leftTabs->setTabPosition(QTabWidget::North);
    m_leftTabs->setMinimumWidth(180);
    m_leftTabs->setMaximumWidth(350);
    m_leftTabs->setStyleSheet(
        "QTabWidget::pane { border: none; }"
        "QTabBar::tab { border: none; padding: 6px 14px; font-size: 12px; }"
        "QTabBar::tab:selected { border-top: 2px solid #6c63ff; }"
        );

    // File tree explorer
    m_fileSystemModel = new QFileSystemModel(this);
    m_fileSystemModel->setRootPath(QDir::homePath());
    m_fileSystemModel->setFilter(QDir::NoDotAndDotDot | QDir::AllDirs | QDir::Files);
    m_fileTree = new QTreeView();
    m_fileTree->setModel(m_fileSystemModel);
    m_fileTree->setRootIndex(m_fileSystemModel->index(QDir::homePath()));
    m_fileTree->setAnimated(true);
    m_fileTree->setIndentation(16);
    m_fileTree->setSortingEnabled(true);
    m_fileTree->setHeaderHidden(true);
    m_fileTree->setColumnHidden(1, true);
    m_fileTree->setColumnHidden(2, true);
    m_fileTree->setColumnHidden(3, true);
    m_fileTree->setStyleSheet("QTreeView { border: none; font-size: 12px; }"
                              "QTreeView::item { padding: 2px 4px; }");
    connect(m_fileTree, &QTreeView::doubleClicked, this, [this](const QModelIndex &idx) {
        QString path = m_fileSystemModel->filePath(idx);
        QFileInfo fi(path);
        if (fi.isFile()) {
            QFile f(path);
            if (f.open(QIODevice::ReadOnly)) {
                QString content = QString::fromUtf8(f.readAll());
                f.close();
                CodeEditor *ed = activeCodeEditor();
                ed->setCode(content);
                ed->setFilePath(path);
                ed->setFocus();
            }
        }
    });
    m_leftTabs->addTab(m_fileTree, "  Explorer  ");

    // Project tab
    m_projectExplorer = new ProjectExplorer();
    connect(m_projectExplorer, &ProjectExplorer::fileSelected, this, [this](const QString &path) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly)) {
            QString content = QString::fromUtf8(f.readAll());
            f.close();
            CodeEditor *ed = activeCodeEditor();
            ed->setCode(content);
            ed->setFilePath(path);
            ed->setFocus();
        }
    });
    m_leftTabs->addTab(m_projectExplorer, "  Project  ");

    // Model status tab
    auto *modelPanel = new QWidget();
    auto *modelLayout = new QVBoxLayout(modelPanel);
    modelLayout->setContentsMargins(8, 8, 8, 8);
    modelLayout->setSpacing(4);

    auto *modelTitle = new QLabel("MODEL STATUS");
    modelTitle->setStyleSheet("font-weight: bold; font-size: 13px; padding: 4px 0; letter-spacing: 1px;");
    modelLayout->addWidget(modelTitle);

    QStringList modelKeys = {"planner", "embed", "coder", "expert", "vision"};
    QStringList modelNames = {"Planner", "Embed", "Coder", "Expert", "Vision"};
    for (int i = 0; i < modelKeys.size(); ++i) {
        auto *row = new QWidget();
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(6);

        auto *dot = new QLabel("●");
        dot->setFixedSize(16, 16);
        dot->setAlignment(Qt::AlignCenter);
        dot->setStyleSheet("font-size: 14px; color: #e06c75;");
        m_modelStatusDots[modelKeys[i]] = dot;
        rowLayout->addWidget(dot);

        auto *label = new QLabel(modelNames[i]);
        rowLayout->addWidget(label);
        rowLayout->addStretch();

        modelLayout->addWidget(row);
    }

    m_startAllBtn = new QPushButton("▶ Start All Models");
    connect(m_startAllBtn, &QPushButton::clicked, this, &MainWindow::startAllModels);
    modelLayout->addWidget(m_startAllBtn);

    m_stopAllBtn = new QPushButton("■ Stop All Models");
    connect(m_stopAllBtn, &QPushButton::clicked, this, &MainWindow::stopAllModels);
    modelLayout->addWidget(m_stopAllBtn);

    auto *startRow = new QWidget();
    auto *startRowLayout = new QHBoxLayout(startRow);
    startRowLayout->setContentsMargins(0, 0, 0, 0);
    startRowLayout->setSpacing(4);

    m_startPlannerBtn = new QPushButton("Start Planner");
    connect(m_startPlannerBtn, &QPushButton::clicked, this, &MainWindow::startPlanner);
    startRowLayout->addWidget(m_startPlannerBtn);

    m_startEmbedBtn = new QPushButton("Start Embed");
    connect(m_startEmbedBtn, &QPushButton::clicked, this, &MainWindow::startEmbed);
    startRowLayout->addWidget(m_startEmbedBtn);

    m_startCoderBtn = new QPushButton("Start Coder");
    connect(m_startCoderBtn, &QPushButton::clicked, this, &MainWindow::startCoder);
    startRowLayout->addWidget(m_startCoderBtn);

    m_startExpertBtn = new QPushButton("Start Expert");
    connect(m_startExpertBtn, &QPushButton::clicked, this, &MainWindow::startExpert);
    startRowLayout->addWidget(m_startExpertBtn);

    modelLayout->addWidget(startRow);

    auto *startRow2 = new QWidget();
    auto *startRowLayout2 = new QHBoxLayout(startRow2);
    startRowLayout2->setContentsMargins(0, 0, 0, 0);
    startRowLayout2->setSpacing(4);

    m_startVisionBtn = new QPushButton("Start Vision");
    connect(m_startVisionBtn, &QPushButton::clicked, this, &MainWindow::startVision);
    startRowLayout2->addWidget(m_startVisionBtn);
    startRowLayout2->addStretch();

    modelLayout->addWidget(startRow2);

    // VRAM detail
    auto *vramTitle = new QLabel("VRAM");
    vramTitle->setStyleSheet("font-weight: bold; font-size: 13px; padding: 4px 0;");
    modelLayout->addWidget(vramTitle);

    m_vramDetailLabel = new QLabel("Checking...");
    m_vramDetailLabel->setStyleSheet("font-size: 14px; font-family: Consolas; padding: 2px 4px; color: #4caf50;");
    modelLayout->addWidget(m_vramDetailLabel);

    // Health
    auto *healthTitle = new QLabel("HEALTH");
    healthTitle->setStyleSheet("font-weight: bold; font-size: 13px; padding: 4px 0;");
    modelLayout->addWidget(healthTitle);

    m_healthLabel = new QLabel("Checking...");
    m_healthLabel->setStyleSheet("color: #ffc107; font-size: 13px; font-family: Consolas; padding: 2px 4px;");
    modelLayout->addWidget(m_healthLabel);

    // Knowledge Base section in model panel
    auto *kbTitle = new QLabel("KNOWLEDGE BASE");
    kbTitle->setStyleSheet("font-weight: bold; font-size: 13px; padding: 4px 0;");
    modelLayout->addWidget(kbTitle);

    auto *importPdfBtn = new QPushButton("+ Import PDF");
    importPdfBtn->setStyleSheet(
        "QPushButton { padding: 5px 10px; font-size: 13px; border-radius: 3px; }"
        );
    connect(importPdfBtn, &QPushButton::clicked, this, &MainWindow::onAttachPdf);
    modelLayout->addWidget(importPdfBtn);

    auto *importImgBtn = new QPushButton("+ Import Picture");
    importImgBtn->setStyleSheet(
        "QPushButton { padding: 5px 10px; font-size: 13px; border-radius: 3px; }"
        );
    connect(importImgBtn, &QPushButton::clicked, this, &MainWindow::onAttachImage);
    modelLayout->addWidget(importImgBtn);

    auto *kbScrollArea = new QScrollArea();
    kbScrollArea->setWidgetResizable(true);
    kbScrollArea->setStyleSheet("QScrollArea { border: none; }");

    m_pdfListContainer = new QWidget();
    m_pdfListLayout = new QVBoxLayout(m_pdfListContainer);
    m_pdfListLayout->setContentsMargins(4, 4, 4, 4);
    m_pdfListLayout->setSpacing(2);
    kbScrollArea->setWidget(m_pdfListContainer);
    modelLayout->addWidget(kbScrollArea, 1);

    modelLayout->addStretch();
    m_leftTabs->addTab(modelPanel, "  Model  ");

    // Monitoring tab
    m_monitorWidget = new MonitorWidget();
    m_leftTabs->addTab(m_monitorWidget, "  Monitoring  ");

    // Git tab
    m_gitPanel = new GitPanel(m_terminalExecutor);
    connect(m_gitPanel, &GitPanel::diffRequested, this, [this](const QString &filePath) {
        if (m_activeEditor) {
            QFile file(filePath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                m_activeEditor->setPlainText(QString::fromUtf8(file.readAll()));
                file.close();
            }
        }
    });
    connect(m_gitPanel, &GitPanel::gitOutput, this, [this](const QString &text) {
        if (m_terminalOutput)
            m_terminalOutput->append("<span style='color:#61afef;'>" + text.toHtmlEscaped() + "</span>");
    });
    m_leftTabs->addTab(m_gitPanel, "  Git  ");

    mainSplitter->addWidget(m_leftTabs);
}

void MainWindow::createCenterPanel(QSplitter *mainSplitter) {
    auto *centerContainer = new QWidget();
    auto *centerLayout = new QVBoxLayout(centerContainer);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    m_centerSplitter = new QSplitter(Qt::Vertical);
    m_centerSplitter->setHandleWidth(1);

    createBottomPanel();

    // Editor container (TOP half)
    auto *editorContainer = new QWidget();
    auto *editorOuterLayout = new QVBoxLayout(editorContainer);
    editorOuterLayout->setContentsMargins(0, 0, 0, 0);
    editorOuterLayout->setSpacing(0);

    // Editor header with split button
    auto *editorHeader = new QWidget();
    auto *editorHeaderLayout = new QHBoxLayout(editorHeader);
    editorHeaderLayout->setContentsMargins(8, 2, 8, 2);
    editorHeaderLayout->setSpacing(4);

    auto *splitBtn = new QPushButton("⤍");
    splitBtn->setFixedSize(24, 24);
    splitBtn->setToolTip("Split editor vertically");
    splitBtn->setStyleSheet(
        "QPushButton { background-color: transparent; border: none; font-size: 16px; }"
        "QPushButton:hover { background-color: #3a3a5a; border-radius: 3px; }");

    editorHeaderLayout->addStretch(1);
    editorHeaderLayout->addWidget(splitBtn);

    editorOuterLayout->addWidget(editorHeader);

    // Vertical split: code editor splitter for side-by-side views
    m_editorSplitter = new QSplitter(Qt::Horizontal);
    m_editorSplitter->setHandleWidth(1);

    m_codeEditor = new CodeEditor();
    m_codeEditor->setReadOnly(false);
    m_codeEditor->setTheme(m_isDark);
    m_editorSplitter->addWidget(m_codeEditor);
    auto syncActiveEditor = [this](CodeEditor *ed) {
        m_activeEditor = ed;
        m_orchestrator->setCurrentEditor(ed->filePath());
    };
    connect(m_codeEditor, &CodeEditor::focused, this, syncActiveEditor);
    m_activeEditor = m_codeEditor;
    m_orchestrator->setCurrentEditor(m_codeEditor->filePath());

    auto syncPaneMatches = [this]() {
        int total = 0;
        for (int i = 0; i < m_editorSplitter->count(); ++i) {
            auto *ed = qobject_cast<CodeEditor *>(m_editorSplitter->widget(i));
            if (ed) total += ed->currentMatchCount();
        }
        for (int i = 0; i < m_editorSplitter->count(); ++i) {
            auto *ed = qobject_cast<CodeEditor *>(m_editorSplitter->widget(i));
            if (ed) ed->setExternalMatchCount(total);
        }
    };
    connect(m_codeEditor, &CodeEditor::matchCountChanged, this, syncPaneMatches);

    editorOuterLayout->addWidget(m_editorSplitter, 1);

    // Split button toggles: adds/removes second editor pane
    connect(splitBtn, &QPushButton::clicked, this, [this, splitBtn, syncPaneMatches, syncActiveEditor]() {
        if (m_editorSplitter->count() == 1) {
            auto *second = new CodeEditor();
            second->setReadOnly(false);
            second->setTheme(m_isDark);
            second->setCode(m_codeEditor->code());
            m_editorSplitter->addWidget(second);
            connect(second, &CodeEditor::focused, this, syncActiveEditor);
            connect(second, &CodeEditor::matchCountChanged, this, syncPaneMatches);
            second->setFocus();
            splitBtn->setText("✕");
            splitBtn->setToolTip("Close split");
        } else {
            // Remove and delete the second editor
            QWidget *w = m_editorSplitter->widget(1);
            // If removing the active editor, fall back to primary
            if (m_activeEditor == w) {
                m_activeEditor = m_codeEditor;
                m_orchestrator->setCurrentEditor(m_codeEditor->filePath());
            }
            w->setParent(nullptr);
            w->deleteLater();
            splitBtn->setText("⤍");
            splitBtn->setToolTip("Split editor vertically");
            m_codeEditor->setFocus();
        }
    });

    m_centerSplitter->addWidget(editorContainer);
    m_centerSplitter->addWidget(m_bottomTabs);
    m_centerSplitter->setStretchFactor(0, 1);
    m_centerSplitter->setStretchFactor(1, 0);
    centerLayout->addWidget(m_centerSplitter, 1);

    createInputBar(centerLayout);

    mainSplitter->addWidget(centerContainer);

    // Show input bar only on Chat tab
    connect(m_bottomTabs, &QTabWidget::currentChanged, this, [this](int idx) {
        m_inputBar->setVisible(idx == 0);
    });
    m_inputBar->setVisible(true); // Chat tab is default

    // Global Ctrl+F / Ctrl+H – route to whichever editor has focus
    auto *globalFindAction = new QAction(this);
    globalFindAction->setShortcut(QKeySequence("Ctrl+F"));
    globalFindAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(globalFindAction);
    connect(globalFindAction, &QAction::triggered, this, [this]() {
        QWidget *w = QApplication::focusWidget();
        while (w) {
            if (auto *ed = qobject_cast<CodeEditor *>(w)) { ed->toggleFind(); return; }
            if (auto *chat = qobject_cast<ChatEditor *>(w)) { chat->toggleFind(); return; }
            w = w->parentWidget();
        }
        if (m_codeEditor) m_codeEditor->toggleFind();
    });
    auto *globalReplaceAction = new QAction(this);
    globalReplaceAction->setShortcut(QKeySequence("Ctrl+H"));
    globalReplaceAction->setShortcutContext(Qt::ApplicationShortcut);
    addAction(globalReplaceAction);
    connect(globalReplaceAction, &QAction::triggered, this, [this]() {
        QWidget *w = QApplication::focusWidget();
        while (w) {
            if (auto *ed = qobject_cast<CodeEditor *>(w)) { ed->toggleReplace(); return; }
            if (auto *chat = qobject_cast<ChatEditor *>(w)) { chat->toggleReplace(); return; }
            w = w->parentWidget();
        }
        if (m_codeEditor) m_codeEditor->toggleReplace();
    });
}

void MainWindow::createBottomPanel() {
    m_bottomTabs = new QTabWidget();
    m_bottomTabs->setMinimumHeight(150);
    m_bottomTabs->setMaximumHeight(400);

    m_bottomStack = new QStackedWidget();

    m_planDisplay = new ChatEditor();
    m_planDisplay->setStyleSheet("border: none; font-size: 13px;");

    m_chatDisplay = new ChatEditor();
    m_chatDisplay->setStyleSheet("border: none; font-size: 13px;");

    m_agentDisplay = new ChatEditor();
    m_agentDisplay->setStyleSheet("border: none; font-size: 13px;");

    m_bottomStack->addWidget(m_planDisplay);   // index 0
    m_bottomStack->addWidget(m_chatDisplay);   // index 1
    m_bottomStack->addWidget(m_agentDisplay);  // index 2
    m_bottomStack->setCurrentIndex(1);

    m_modelLogDisplay = new ChatEditor();
    m_modelLogDisplay->setStyleSheet("border: none; font-size: 12px; font-family: Consolas;");

    // Terminal tab
    auto *terminalContainer = new QWidget();
    auto *terminalLayout = new QVBoxLayout(terminalContainer);
    terminalLayout->setContentsMargins(0, 0, 0, 0);
    terminalLayout->setSpacing(0);

    m_terminalOutput = new ChatEditor();
    m_terminalOutput->setStyleSheet(
        "QTextEdit { border: none; "
        "font-size: 13px; font-family: 'Ubuntu Mono', 'Consolas', monospace; }");
    m_terminalOutput->setPlaceholderText("Terminal output appears here...");
    terminalLayout->addWidget(m_terminalOutput, 1);

    // Build status bar
    auto *statusBar = new QWidget();
    statusBar->setFixedHeight(28);
    auto *statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(8, 0, 8, 0);

    m_buildStatusLabel = new QLabel("Idle");
    m_buildStatusLabel->setStyleSheet("font-size: 11px;");

    m_terminalCwdLabel = new QLabel();
    m_terminalCwdLabel->setStyleSheet("font-size: 11px; color: #98c379; padding: 0 8px;");
    QString initCwd = m_currentBuildDir.isEmpty() ?
                          QApplication::applicationDirPath() : m_currentBuildDir;
    m_terminalCwdLabel->setText(initCwd);
    m_terminalCwdLabel->setToolTip("Current working directory");

    QString termBtnStyle =
        "QPushButton { padding: 2px 8px; font-size: 11px; border-radius: 3px; }";
    auto *buildBtn = new QPushButton("Build");
    buildBtn->setFixedWidth(60);
    buildBtn->setStyleSheet(termBtnStyle);

    auto *runBtn = new QPushButton("Run");
    runBtn->setFixedWidth(60);
    runBtn->setStyleSheet(termBtnStyle);

    auto *gitBtn = new QPushButton("Git");
    gitBtn->setFixedWidth(60);
    gitBtn->setStyleSheet(termBtnStyle);

    QPushButton *clearBtn = new QPushButton("Clear");
    clearBtn->setFixedWidth(60);
    clearBtn->setStyleSheet(termBtnStyle);

    connect(buildBtn, &QPushButton::clicked, this, &MainWindow::onBuildClicked);
    connect(runBtn, &QPushButton::clicked, this, [this]() { onRunClicked(); });
    connect(gitBtn, &QPushButton::clicked, this, &MainWindow::onGitClicked);
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        m_terminalOutput->clear();
    });

    statusLayout->addWidget(m_terminalCwdLabel, 1);
    statusLayout->addWidget(m_buildStatusLabel);
    statusLayout->addWidget(buildBtn);
    statusLayout->addWidget(runBtn);
    statusLayout->addWidget(gitBtn);
    statusLayout->addWidget(clearBtn);
    terminalLayout->addWidget(statusBar);

    // Terminal input bar
    auto *termInputBar = new QWidget();
    auto *termInputLayout = new QHBoxLayout(termInputBar);
    termInputLayout->setContentsMargins(8, 4, 8, 4);

    m_terminalInput = new QLineEdit();
    m_terminalInput->setPlaceholderText("$ type a shell command...");
    m_terminalInput->setStyleSheet(
        "QLineEdit { border-radius: 3px; padding: 4px 8px; font-size: 13px; font-family: Consolas; }"
        "QLineEdit:focus { border-color: #6c63ff; }");

    m_terminalSendBtn = new QPushButton("⮐");
    m_terminalSendBtn->setFixedSize(32, 28);
    m_terminalSendBtn->setToolTip("Execute command");
    m_terminalSendBtn->setStyleSheet(
        "QPushButton { border: none; border-radius: 3px; font-size: 16px; }");

    connect(m_terminalSendBtn, &QPushButton::clicked, this, &MainWindow::onTerminalCommand);
    connect(m_terminalInput, &QLineEdit::returnPressed, this, &MainWindow::onTerminalCommand);

    termInputLayout->addWidget(m_terminalInput, 1);
    termInputLayout->addWidget(m_terminalSendBtn);
    terminalLayout->addWidget(termInputBar);

    m_bottomTabs->addTab(m_bottomStack, "Chat");
    m_bottomTabs->addTab(m_modelLogDisplay, "Model Logs");
    m_bottomTabs->addTab(terminalContainer, "Terminal");
}

void MainWindow::createInputBar(QVBoxLayout *centerLayout) {
    m_inputBar = new QWidget();
    auto *inputLayout = new QHBoxLayout(m_inputBar);
    inputLayout->setContentsMargins(8, 6, 8, 6);

    m_userInput = new QLineEdit();
    m_userInput->setPlaceholderText("Type a message...");
    m_userInput->setStyleSheet(
        "QLineEdit { border-radius: 4px; padding: 6px 10px; font-size: 14px; }"
        "QLineEdit:focus { border-color: #6c63ff; }"
        );

    m_sendBtn = new QPushButton("Send");
    m_sendBtn->setFixedWidth(72);
    m_sendBtn->setStyleSheet(
        "QPushButton { background-color: #6c63ff; color: #fff; border: none; "
        "padding: 6px 12px; font-size: 14px; font-weight: bold; border-radius: 4px; }"
        "QPushButton:hover { background-color: #7c73ff; }"
        "QPushButton:pressed { background-color: #5c53ef; }"
        );

    m_clearChatBtn = new QPushButton("🗑");
    m_clearChatBtn->setFixedSize(36, 36);
    m_clearChatBtn->setToolTip("Clear chat history");
    m_clearChatBtn->setStyleSheet(
        "QPushButton { border: none; border-radius: 4px; font-size: 16px; }"
        "QPushButton:hover { background-color: #e06c75; color: #fff; }"
        );

    m_programBtn = new QPushButton("Program");
    m_programBtn->setCheckable(true);
    m_programBtn->setFixedWidth(72);
    m_programBtn->setToolTip("Program mode: Planner + Coder workflow");
    m_programBtn->setStyleSheet(
        "QPushButton { background-color: #2a2a4a; color: #c0c0e0; border: 1px solid #3a3a5a; "
        "padding: 6px 8px; font-size: 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #3a3a5a; }"
        "QPushButton:checked { background-color: #6c63ff; color: #fff; border-color: #6c63ff; }"
        );

    m_pictureBtn = new QPushButton("Picture");
    m_pictureBtn->setCheckable(true);
    m_pictureBtn->setFixedWidth(66);
    m_pictureBtn->setToolTip("Picture mode: Planner + Embed + Vision workflow");
    m_pictureBtn->setStyleSheet(
        "QPushButton { background-color: #2a2a4a; color: #c0c0e0; border: 1px solid #3a3a5a; "
        "padding: 6px 8px; font-size: 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #3a3a5a; }"
        "QPushButton:checked { background-color: #6c63ff; color: #fff; border-color: #6c63ff; }"
        );

    m_pdfViewerBtn = new QPushButton("PDF View");
    m_pdfViewerBtn->setCheckable(true);
    m_pdfViewerBtn->setFixedWidth(66);
    m_pdfViewerBtn->setToolTip("PDF Viewer mode: Query loaded PDFs with Planner + Embed + Coder");
    m_pdfViewerBtn->setStyleSheet(
        "QPushButton { background-color: #2a2a4a; color: #c0c0e0; border: 1px solid #3a3a5a; "
        "padding: 6px 8px; font-size: 12px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #3a3a5a; }"
        "QPushButton:checked { background-color: #6c63ff; color: #fff; border-color: #6c63ff; }"
        );

    m_imageBtn = new QPushButton("🖼");
    m_imageBtn->setFixedSize(40, 36);
    m_imageBtn->setToolTip("Image Q&A: type a question, then click to select an image and get an answer");
    m_imageBtn->setStyleSheet(
        "QPushButton { border: none; border-radius: 4px; font-size: 16px; }"
        );

    m_pdfBtn = new QPushButton("📄");
    m_pdfBtn->setFixedSize(40, 36);
    m_pdfBtn->setToolTip("PDF Q&A: type a question, then click to select a PDF and get an answer");
    m_pdfBtn->setStyleSheet(
        "QPushButton { border: none; border-radius: 4px; font-size: 16px; }"
        );

    m_imagePreview = new QLabel();
    m_imagePreview->setFixedSize(32, 32);
    m_imagePreview->setStyleSheet("border: 1px solid; border-radius: 4px;");
    m_imagePreview->hide();

    inputLayout->addWidget(m_userInput, 1);
    inputLayout->addWidget(m_sendBtn);
    inputLayout->addWidget(m_clearChatBtn);
    inputLayout->addWidget(m_programBtn);
    inputLayout->addWidget(m_pictureBtn);
    inputLayout->addWidget(m_pdfViewerBtn);
    inputLayout->addWidget(m_imageBtn);
    inputLayout->addWidget(m_pdfBtn);
    inputLayout->addWidget(m_imagePreview);
    centerLayout->addWidget(m_inputBar);

    connect(m_sendBtn, &QPushButton::clicked, this, &MainWindow::onSendClicked);
    connect(m_clearChatBtn, &QPushButton::clicked, this, &MainWindow::onClearChat);
    connect(m_userInput, &QLineEdit::returnPressed, this, &MainWindow::onSendClicked);
    connect(m_imageBtn, &QPushButton::clicked, this, &MainWindow::onImageQnA);
    connect(m_pdfBtn, &QPushButton::clicked, this, &MainWindow::onPdfQnA);
    connect(m_programBtn, &QPushButton::clicked, this, &MainWindow::onProgramClicked);
    connect(m_pictureBtn, &QPushButton::clicked, this, &MainWindow::onPictureClicked);
    connect(m_pdfViewerBtn, &QPushButton::clicked, this, &MainWindow::onPdfViewerClicked);
}

// ── Theme ─────────────────────────────────────────────────

void MainWindow::applyTheme() {
    if (m_isDark) {
        QString dark = R"(
            QMainWindow, QWidget { background-color: #12121e; color: #c0c0e0; }
            QLabel { color: #c0c0e0; }
            QTextEdit { background-color: #1a1a2e; color: #d0d0e4; border: none; font-size: 13px; }
            QLineEdit { background-color: #1e1e38; color: #d0d0e4; border: 1px solid #2a2a4a; border-radius: 3px; padding: 4px 8px; }
            QLineEdit:focus { border-color: #6c63ff; }
            QPushButton { background-color: #2a2a4a; color: #c0c0e0; border: 1px solid #3a3a5a; padding: 5px 10px; border-radius: 3px; }
            QPushButton:hover { background-color: #3a3a5a; }
            QRadioButton { color: #7a7a9e; font-size: 11px; padding: 2px 10px; border: 1px solid #2a2a4a; border-radius: 3px; background-color: #1e1e32; }
            QRadioButton::indicator { width: 0; height: 0; }
            QRadioButton:checked { color: #e0e0f0; background-color: #3a3a5a; border-color: #6c63ff; }
            QRadioButton:hover { color: #c0c0e0; }
            QTabWidget::pane { border: none; background-color: #1a1a2e; }
            QTabBar::tab { background-color: #1e1e32; color: #9e9ebc; border: none; padding: 6px 14px; font-size: 12px; }
            QTabBar::tab:selected { background-color: #222238; color: #e0e0f0; border-top: 2px solid #6c63ff; }
            QTabBar::tab:hover { color: #e0e0f0; }
            QSplitter::handle { background-color: #1e1e38; }
            QScrollArea { background-color: #1a1a2e; border: none; }
            QStackedWidget { background-color: #1a1a2e; }
            QMenuBar { background-color: #16162a; color: #c0c0e0; border-bottom: 1px solid #1e1e38; padding: 2px 0; font-size: 12px; }
            QMenuBar::item:selected { background-color: #3a3a5a; }
            QMenu { background-color: #14142a; color: #c0c0e0; border: 1px solid #2a2a4a; padding: 4px; }
            QMenu::item:selected { background-color: #3a3a5a; }
            QMenu::separator { height: 1px; background-color: #2a2a4a; margin: 4px 8px; }
            QSpinBox, QDoubleSpinBox { background-color: #222238; color: #d0d0e4; border: 1px solid #2a2a4a; border-radius: 3px; }
            QComboBox { background-color: #222238; color: #d0d0e4; border: 1px solid #2a2a4a; border-radius: 3px; padding: 4px; }
            QCheckBox { color: #c0c0e0; spacing: 6px; }
            QGroupBox { color: #8a8aaa; font-weight: bold; border: 1px solid #2a2a4a; border-radius: 4px; margin-top: 12px; padding-top: 14px; }
            QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
        )";
        setStyleSheet(dark);
        m_themeBtn->setText("☀");
    } else {
        QString light = R"(
            QMainWindow, QWidget { background-color: #f0f0f0; color: #333333; }
            QLabel { color: #333333; }
            QTextEdit { background-color: #ffffff; color: #333333; border: none; font-size: 13px; }
            QLineEdit { background-color: #ffffff; color: #333333; border: 1px solid #cccccc; border-radius: 3px; padding: 4px 8px; }
            QLineEdit:focus { border-color: #6c63ff; }
            QPushButton { background-color: #e0e0e0; color: #333333; border: 1px solid #cccccc; padding: 5px 10px; border-radius: 3px; }
            QPushButton:hover { background-color: #d0d0d0; }
            QRadioButton { color: #666666; font-size: 11px; padding: 2px 10px; border: 1px solid #cccccc; border-radius: 3px; background-color: #ffffff; }
            QRadioButton::indicator { width: 0; height: 0; }
            QRadioButton:checked { color: #333333; background-color: #e0e0e0; border-color: #6c63ff; }
            QRadioButton:hover { color: #333333; }
            QTabWidget::pane { border: none; background-color: #ffffff; }
            QTabBar::tab { background-color: #e8e8e8; color: #666666; border: none; padding: 6px 14px; font-size: 12px; }
            QTabBar::tab:selected { background-color: #ffffff; color: #333333; border-top: 2px solid #6c63ff; }
            QTabBar::tab:hover { color: #333333; }
            QSplitter::handle { background-color: #cccccc; }
            QScrollArea { background-color: #ffffff; border: none; }
            QStackedWidget { background-color: #ffffff; }
            QMenuBar { background-color: #e0e0e0; color: #333333; border-bottom: 1px solid #cccccc; padding: 2px 0; font-size: 12px; }
            QMenuBar::item:selected { background-color: #d0d0d0; }
            QMenu { background-color: #ffffff; color: #333333; border: 1px solid #cccccc; padding: 4px; }
            QMenu::item:selected { background-color: #e0e0e0; }
            QMenu::separator { height: 1px; background-color: #cccccc; margin: 4px 8px; }
            QSpinBox, QDoubleSpinBox { background-color: #ffffff; color: #333333; border: 1px solid #cccccc; border-radius: 3px; }
            QComboBox { background-color: #ffffff; color: #333333; border: 1px solid #cccccc; border-radius: 3px; padding: 4px; }
            QCheckBox { color: #333333; spacing: 6px; }
            QGroupBox { color: #666666; font-weight: bold; border: 1px solid #cccccc; border-radius: 4px; margin-top: 12px; padding-top: 14px; }
            QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }
        )";
        setStyleSheet(light);
        m_themeBtn->setText("🌙");
    }
    m_topBar->setStyleSheet(m_isDark ?
                                "background-color: #16162a;" :
                                "background-color: #e0e0e0;");
    m_appIcon->setStyleSheet(m_isDark ?
                                 "color: #6c63ff; font-weight: bold; font-size: 15px;" :
                                 "color: #6c63ff; font-weight: bold; font-size: 15px;");
    m_themeBtn->setStyleSheet(m_isDark ?
                                  "QPushButton { background-color: transparent; color: #c0c0e0; border: none; font-size: 16px; border-radius: 4px; } QPushButton:hover { background-color: #3a3a5a; }" :
                                  "QPushButton { background-color: transparent; color: #666666; border: none; font-size: 16px; border-radius: 4px; } QPushButton:hover { background-color: #d0d0d0; }");

    if (m_monitorWidget)
        m_monitorWidget->setDark(m_isDark);
    if (m_codeEditor)
        m_codeEditor->setTheme(m_isDark);
    if (m_chatDisplay)
        m_chatDisplay->setTheme(m_isDark);
    if (m_planDisplay)
        m_planDisplay->setTheme(m_isDark);
    if (m_agentDisplay)
        m_agentDisplay->setTheme(m_isDark);
    if (m_modelLogDisplay)
        m_modelLogDisplay->setTheme(m_isDark);
    if (m_terminalOutput)
        m_terminalOutput->setTheme(m_isDark);
    if (m_terminalCwdLabel)
        m_terminalCwdLabel->setStyleSheet(m_isDark ?
                                              "font-size: 11px; color: #98c379; padding: 0 8px;" :
                                              "font-size: 11px; color: #2e7d32; padding: 0 8px;");
    if (m_editorSplitter) {
        for (int i = 0; i < m_editorSplitter->count(); ++i) {
            auto *ed = qobject_cast<CodeEditor *>(m_editorSplitter->widget(i));
            if (ed) ed->setTheme(m_isDark);
        }
    }
    if (m_projectExplorer)
        m_projectExplorer->setTheme(m_isDark);
    if (m_gitPanel)
        m_gitPanel->setDark(m_isDark);
    updateModelStatusUI();
}

// ── Slots ─────────────────────────────────────────────────

void MainWindow::onSendClicked() {
    QString query = m_userInput->text().trimmed();
    if (query.isEmpty() && m_workflowMode != WorkflowMode::Picture) return;

    if (m_workflowMode == WorkflowMode::Program) {
        handleProgramSend(query);
    } else if (m_workflowMode == WorkflowMode::Picture) {
        handlePictureSend(query);
    } else if (m_workflowMode == WorkflowMode::PdfViewer) {
        handlePdfViewerSend(query);
    } else {
        handleNormalSend(query);
    }
    m_userInput->clear();
}

void MainWindow::onClearChat() {
    if (m_chatDisplay) m_chatDisplay->clear();
    if (m_planDisplay) m_planDisplay->clear();
    if (m_agentDisplay) m_agentDisplay->clear();
    appendChat("System", "Chat cleared.", "#98c379");
}

void MainWindow::handleNormalSend(const QString &query) {
    if (query.isEmpty()) return;
    appendChat("User", query, m_isDark ? "#61afef" : "#1565c0");

    int modeId = m_modeGroup->checkedId();
    Orchestrator::Mode mode;
    if (modeId == 0) {
        mode = Orchestrator::Mode::Chat;
        m_bottomStack->setCurrentIndex(1);
    } else if (modeId == 1) {
        mode = Orchestrator::Mode::Plan;
        m_bottomStack->setCurrentIndex(0);
    } else {
        mode = Orchestrator::Mode::Agent;
        m_bottomStack->setCurrentIndex(2);
    }

    QString projectDir = m_currentBuildDir;
    if (projectDir.isEmpty() && activeCodeEditor() && !activeCodeEditor()->filePath().isEmpty()) {
        projectDir = QFileInfo(activeCodeEditor()->filePath()).path();
    }
    m_orchestrator->setProjectDir(projectDir);

    if (!m_attachedImage.isNull()) {
        QString lowerQuery = query.toLower();
        bool isImageQuestion = lowerQuery.contains("image") || lowerQuery.contains("تصویر") ||
                               lowerQuery.contains("picture") || lowerQuery.contains("عکس") ||
                               lowerQuery.contains("photo") || lowerQuery.contains("تصویری") ||
                               lowerQuery.contains("describe") || lowerQuery.contains("توصیف");

        if (isImageQuestion) {
            m_orchestrator->processQueryWithImage(query, m_attachedImage, mode, m_attachedImagePath);
        } else {
            m_orchestrator->processQuery(query, mode, activeCodeEditor()->code());
        }
        m_attachedImage = QImage();
        m_attachedImagePath.clear();
        m_imagePreview->hide();
    } else {
        m_orchestrator->processQuery(query, mode, activeCodeEditor()->code());
    }
}

void MainWindow::handleProgramSend(const QString &query) {
    if (query.isEmpty()) return;
    appendChat("User", "[Program] " + query, m_isDark ? "#e5c07b" : "#e65100");

    int modeId = m_modeGroup->checkedId();
    Orchestrator::Mode mode;
    if (modeId == 0) {
        mode = Orchestrator::Mode::Chat;
        m_bottomStack->setCurrentIndex(1);
    } else if (modeId == 1) {
        mode = Orchestrator::Mode::Plan;
        m_bottomStack->setCurrentIndex(0);
    } else {
        mode = Orchestrator::Mode::Agent;
        m_bottomStack->setCurrentIndex(2);
    }

    QString code = activeCodeEditor()->code();
    m_orchestrator->processProgramQuery(query, code, mode);
}

void MainWindow::handlePictureSend(const QString &query) {
    if (m_attachedImage.isNull()) {
        appendChat("System", "Please attach an image first using the 🖼 button.", "#e06c75");
        return;
    }
    appendChat("User", "[Picture] " + (query.isEmpty() ? "Describe this image" : query), m_isDark ? "#e5c07b" : "#e65100");

    Orchestrator::Mode mode = Orchestrator::Mode::Chat;
    m_bottomStack->setCurrentIndex(1);

    m_orchestrator->processQueryWithImage(query.isEmpty() ? "Describe this image in detail." : query,
                                          m_attachedImage, mode, m_attachedImagePath);
    m_attachedImage = QImage();
    m_attachedImagePath.clear();
    m_imagePreview->hide();
}

void MainWindow::handlePdfViewerSend(const QString &query) {
    if (query.isEmpty()) return;
    appendChat("User", "[PDF Viewer] " + query, m_isDark ? "#e5c07b" : "#e65100");

    int modeId = m_modeGroup->checkedId();
    Orchestrator::Mode mode;
    if (modeId == 0) {
        mode = Orchestrator::Mode::Chat;
        m_bottomStack->setCurrentIndex(1);
    } else if (modeId == 1) {
        mode = Orchestrator::Mode::Plan;
        m_bottomStack->setCurrentIndex(0);
    } else {
        mode = Orchestrator::Mode::Agent;
        m_bottomStack->setCurrentIndex(2);
    }

    m_orchestrator->processPdfQuery(query, mode);
}

void MainWindow::onProgramClicked() {
    bool checked = m_programBtn->isChecked();
    if (checked) {
        m_pictureBtn->setChecked(false);
        m_pdfViewerBtn->setChecked(false);
        m_workflowMode = WorkflowMode::Program;
        appendChat("System", "Program mode activated: Planner + Embed + Coder loaded. "
                            "Type a message and press Send. Use Chat/Plan/Agent buttons to choose behavior.",
                   "#e5c07b");
        m_vramManager->ensureModel("planner");
        m_vramManager->ensureModel("embed");
        m_vramManager->ensureModel("coder");
    } else {
        m_workflowMode = WorkflowMode::None;
        appendChat("System", "Program mode deactivated.", "#abb2bf");
    }
    updateModelStatusUI();
}

void MainWindow::onPictureClicked() {
    bool checked = m_pictureBtn->isChecked();
    if (checked) {
        m_programBtn->setChecked(false);
        m_pdfViewerBtn->setChecked(false);
        m_workflowMode = WorkflowMode::Picture;
        appendChat("System", "Picture mode activated: Planner + Embed + Vision loaded. "
                            "Attach an image, type a query, and press Send.",
                   "#e5c07b");
        m_vramManager->ensureModel("planner");
        m_vramManager->ensureModel("embed");
        m_vramManager->ensureModel("vision");
    } else {
        m_workflowMode = WorkflowMode::None;
        appendChat("System", "Picture mode deactivated.", "#abb2bf");
    }
    updateModelStatusUI();
}

void MainWindow::onPdfViewerClicked() {
    bool checked = m_pdfViewerBtn->isChecked();
    if (checked) {
        m_programBtn->setChecked(false);
        m_pictureBtn->setChecked(false);
        m_workflowMode = WorkflowMode::PdfViewer;
        appendChat("System", "PDF Viewer mode activated: Planner + Embed + Coder loaded. "
                            "Type a question about loaded PDFs and press Send.",
                   "#e5c07b");
        m_vramManager->ensureModel("planner");
        m_vramManager->ensureModel("embed");
        m_vramManager->ensureModel("coder");
    } else {
        m_workflowMode = WorkflowMode::None;
        appendChat("System", "PDF Viewer mode deactivated.", "#abb2bf");
    }
    updateModelStatusUI();
}

void MainWindow::onImageQnA() {
    QString query = m_userInput->text().trimmed();

    QFileDialog dlg(this, "Select Image for Q&A", "", "Images (*.png *.jpg *.jpeg *.bmp)");
    dlg.setOption(QFileDialog::DontUseNativeDialog);
    dlg.setFileMode(QFileDialog::ExistingFile);
    if (dlg.exec() != QDialog::Accepted) return;
    QString path = dlg.selectedFiles().first();

    QImage img(path);
    if (img.isNull()) {
        LOG_ERROR("Vision", "Failed to load image: " + path);
        return;
    }

    if (!query.isEmpty()) {
        appendChat("User", query + " [Image: " + path.section('/', -1) + "]", "#61afef");
        m_userInput->clear();

        int modeId = m_modeGroup->checkedId();
        Orchestrator::Mode mode;
        if (modeId == 0) {
            mode = Orchestrator::Mode::Chat;
            m_bottomStack->setCurrentIndex(1);
        } else if (modeId == 1) {
            mode = Orchestrator::Mode::Plan;
            m_bottomStack->setCurrentIndex(0);
        } else {
            mode = Orchestrator::Mode::Agent;
            m_bottomStack->setCurrentIndex(2);
        }
        m_orchestrator->processQueryWithImage(query, img, mode, path);
    } else {
        appendChat("User", "[Image: " + path.section('/', -1) + "]", "#61afef");
        int modeId = m_modeGroup->checkedId();
        Orchestrator::Mode mode;
        if (modeId == 0) {
            mode = Orchestrator::Mode::Chat;
            m_bottomStack->setCurrentIndex(1);
        } else if (modeId == 1) {
            mode = Orchestrator::Mode::Plan;
            m_bottomStack->setCurrentIndex(0);
        } else {
            mode = Orchestrator::Mode::Agent;
            m_bottomStack->setCurrentIndex(2);
        }
        m_orchestrator->processQueryWithImage("Describe this image in detail.", img, mode, path);
        m_userInput->clear();
    }
}

void MainWindow::onPdfQnA() {
    QString query = m_userInput->text().trimmed();
    QString path;

    if (!m_selectedPdf.isEmpty() && !query.isEmpty()) {
        auto docs = m_orchestrator->vectorDb()->listDocuments();
        for (const auto &doc : docs) {
            if (doc.filename == m_selectedPdf) {
                path = doc.filepath;
                break;
            }
        }
    }

    if (path.isEmpty()) {
        QFileDialog dlg(this, "Select PDF for Q&A", "", "PDF Files (*.pdf)");
        dlg.setOption(QFileDialog::DontUseNativeDialog);
        dlg.setFileMode(QFileDialog::ExistingFile);
        if (dlg.exec() != QDialog::Accepted) return;
        path = dlg.selectedFiles().first();
        m_selectedPdf = path.section('/', -1);
    }

    if (!query.isEmpty()) {
        appendChat("User", query + " [PDF: " + m_selectedPdf + "]", "#61afef");
        m_userInput->clear();

        int modeId = m_modeGroup->checkedId();
        Orchestrator::Mode mode;
        if (modeId == 0) {
            mode = Orchestrator::Mode::Chat;
            m_bottomStack->setCurrentIndex(1);
        } else if (modeId == 1) {
            mode = Orchestrator::Mode::Plan;
            m_bottomStack->setCurrentIndex(0);
        } else {
            mode = Orchestrator::Mode::Agent;
            m_bottomStack->setCurrentIndex(2);
        }
        m_orchestrator->processQueryWithPdf(path, query, mode, activeCodeEditor()->code());
    } else {
        LOG_INFO("PDF", QString("Selected: %1").arg(m_selectedPdf));
        m_orchestrator->processPdf(path);
    }
}

void MainWindow::onAttachImage() {
    QFileDialog dlg(this, "Select Image", "", "Images (*.png *.jpg *.jpeg *.bmp)");
    dlg.setOption(QFileDialog::DontUseNativeDialog);
    dlg.setFileMode(QFileDialog::ExistingFile);
    if (dlg.exec() != QDialog::Accepted) return;
    QString path = dlg.selectedFiles().first();

    m_attachedImage = QImage(path);
    if (m_attachedImage.isNull()) {
        LOG_ERROR("Vision", "Failed to load image: " + path);
        return;
    }
    m_attachedImagePath = path;

    m_imagePreview->setPixmap(QPixmap::fromImage(
        m_attachedImage.scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    m_imagePreview->show();

    LOG_INFO("Vision", "Image attached: " + path.section('/', -1));
}

void MainWindow::onAttachPdf() {
    QFileDialog dlg(this, "Select PDF", "", "PDF Files (*.pdf)");
    dlg.setOption(QFileDialog::DontUseNativeDialog);
    dlg.setFileMode(QFileDialog::ExistingFile);
    if (dlg.exec() != QDialog::Accepted) return;
    QString path = dlg.selectedFiles().first();
    QString filename = path.section('/', -1);

    LOG_INFO("PDF", QString("Selected: %1").arg(filename));
    m_selectedPdf = filename;
    appendChat("System", QString("Processing PDF: %1...").arg(filename), "#e5c07b");
    m_pdfBtn->setEnabled(false);
    m_pdfBtn->setText("⏳");
    m_orchestrator->processPdf(path);
}

void MainWindow::onRemovePdf() {
    auto *btn = qobject_cast<QPushButton*>(sender());
    if (!btn) return;
    QString filename = btn->property("filename").toString();
    if (filename.isEmpty()) return;

    LOG_INFO("PDF", QString("Removing: %1").arg(filename));
    m_orchestrator->vectorDb()->removeDocumentByName(filename);
    if (m_selectedPdf == filename)
        m_selectedPdf.clear();
    refreshPdfList();
}

void MainWindow::refreshPdfList() {
    // Clear existing items
    QLayoutItem *child;
    while ((child = m_pdfListLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }

    auto docs = m_orchestrator->vectorDb()->listDocuments();
    if (docs.isEmpty()) {
        auto *emptyLabel = new QLabel("No PDFs imported");
        emptyLabel->setStyleSheet("font-size: 11px; padding: 8px;");
        emptyLabel->setAlignment(Qt::AlignCenter);
        m_pdfListLayout->addWidget(emptyLabel);
    } else {
        for (const auto &doc : docs) {
            auto *row = new QWidget();
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(4, 2, 4, 2);

            auto *label = new QPushButton(QString("%1 [%2]").arg(doc.filename).arg(doc.totalChunks));
            label->setStyleSheet(
                "QPushButton { "
                "  background: transparent; border: none; "
                "  font-size: 11px; text-align: left; "
                "  padding: 2px 4px; "
                "}"
                "QPushButton:hover { background: #2a2a3e; border-radius: 3px; }"
            );
            label->setCursor(Qt::PointingHandCursor);
            bool isSelected = (m_selectedPdf == doc.filename);
            if (isSelected) {
                label->setStyleSheet(
                    "QPushButton { "
                    "  background: #1a3a1a; border: 1px solid #98c379; "
                    "  border-radius: 3px; font-size: 11px; text-align: left; "
                    "  padding: 2px 4px; color: #98c379; "
                    "}"
                    "QPushButton:hover { background: #1a4a1a; }"
                );
            }
            label->setProperty("filename", doc.filename);
            connect(label, &QPushButton::clicked, this, [this, doc]() {
                m_selectedPdf = doc.filename;
                appendChat("System", QString("PDF selected: %1\nYou can now ask questions about this document.").arg(doc.filename), "#61afef");
                if (m_modelLogDisplay)
                    m_modelLogDisplay->append(QString("<span style='color:#61afef;'>[PDF] Selected: %1</span>").arg(doc.filename.toHtmlEscaped()));
                refreshPdfList();
            });

            auto *removeBtn = new QPushButton("✕");
            removeBtn->setFixedSize(32, 32);
            removeBtn->setStyleSheet(
                isSelected ?
                "QPushButton { background: transparent; border: none; "
                "font-size: 11px; color: #98c379; } QPushButton:hover { color: #ff8c8c; }" :
                "QPushButton { background: transparent; border: none; "
                "font-size: 11px; color: #e06c75; } QPushButton:hover { color: #ff8c8c; }");
            removeBtn->setProperty("filename", doc.filename);
            connect(removeBtn, &QPushButton::clicked, this, &MainWindow::onRemovePdf);

            rowLayout->addWidget(label, 1);
            rowLayout->addWidget(removeBtn);
            m_pdfListLayout->addWidget(row);
        }
    }
}

void MainWindow::onModeChanged(int id) {
    if (id == 0) {
        m_bottomStack->setCurrentIndex(1);
        LOG_INFO("Mode", "Switched to Chat");
    } else if (id == 1) {
        m_bottomStack->setCurrentIndex(0);
        LOG_INFO("Mode", "Switched to Plan");
    } else {
        m_bottomStack->setCurrentIndex(2);
        LOG_INFO("Mode", "Switched to Agent");
    }
}

void MainWindow::onVRAMUpdate() {
    refreshVRAMAsync();
}

void MainWindow::refreshVRAMAsync() {
    if (m_vramBusy) return;
    m_vramBusy = true;

    if (!m_vramProcess) {
        m_vramProcess = new QProcess(this);
        connect(m_vramProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode, QProcess::ExitStatus) {
                    m_vramBusy = false;
                    if (exitCode == 0) {
                        QString output = QString::fromUtf8(m_vramProcess->readAllStandardOutput()).trimmed();
                        QStringList parts = output.split(", ");
                        if (parts.size() == 3) {
                            QString text = QString("%1 / %2 MB (Free: %3 MB)")
                            .arg(parts[0], parts[1], parts[2]);
                            m_vramLabel->setText("VRAM: " + text);
                            m_vramDetailLabel->setText(text);
                        }
                    }
                });
    }

    m_vramProcess->start("nvidia-smi", {"--query-gpu=memory.used,memory.total,memory.free",
                                        "--format=csv,noheader,nounits"});
}

void MainWindow::onModelLog(const QString &source, const QString &message) {
    LOG_INFO(source, message);
}

void MainWindow::onModelStarted(int port, const QString &name) {
    LOG_INFO("System", QString("%1 started").arg(name));
    if (m_portToKey.contains(port))
        m_failedModels.removeAll(m_portToKey[port]);
    updateModelStatusUI();
    m_healthLabel->setText(QString("%1: RUNNING").arg(name));
    advanceStartAllQueue(port);
}

void MainWindow::onModelStopped(int port, const QString &name) {
    Q_UNUSED(port)
    LOG_WARN("System", name);
    updateModelStatusUI();
}

static QString markdownToHtml(const QString &text);

void MainWindow::appendChat(const QString &role, const QString &text, const QString &color) {
    QTextEdit *display = m_chatDisplay;
    if (m_modeGroup->checkedId() == 1)
        display = m_planDisplay;
    else if (m_modeGroup->checkedId() == 2)
        display = m_agentDisplay;

    QString bodyColor = m_isDark ? "#d0d0e4" : "#333333";
    QString html = QString(
                       "<table style='margin:4px 0; width:100%; border:none;'>"
                       "<tr><td style='color:%1; font-weight:bold; width:1%; white-space:nowrap; vertical-align:top; padding-right:8px;'>%2:</td>"
                       "<td style='color:%3; width:99%;'>%4</td></tr>"
                       "</table>"
                       ).arg(color, role.toHtmlEscaped(), bodyColor, markdownToHtml(text));

    display->insertHtml(html + "<br>");
    display->verticalScrollBar()->setValue(display->verticalScrollBar()->maximum());
}

// ============================================================
// بخش‌های اصلاح‌شده در MainWindow.cpp
// ============================================================

// ─── تابع cleanModelOutput ──────────────────────────────────
static QString cleanModelOutput(const QString &text) {
    QString cleaned = text;
    // حذف تگ‌های رنگی اضافی که ممکن است از مدل بیایند
    cleaned.replace(QRegularExpression("'color:#[0-9a-fA-F]{6};'>"), "");
    cleaned.replace(QRegularExpression("<span style='color:#[0-9a-fA-F]{6};'>"), "<span style='color:inherit;'>");
    cleaned.replace(QRegularExpression("</?span[^>]*>"), "");
    return cleaned;
}

// ─── تابع highlightCpp (اصلاح‌شده) ────────────────────────
static QString highlightCpp(const QString &code) {
    QString h = code;
    
    // ✅ اول: escape کاراکترهای خاص
    h.replace("&", "&amp;");
    h.replace("<", "&lt;");
    h.replace(">", "&gt;");
    
    QStringList keywords = {
        "alignas","alignof","auto","bool","break","case","catch","char","class","const",
        "constexpr","continue","decltype","default","delete","do","double","else","enum",
        "explicit","export","extern","false","float","for","friend","goto","if","inline",
        "int","long","mutable","namespace","new","noexcept","nullptr","operator","override",
        "private","protected","public","register","return","short","signed","sizeof",
        "static","struct","switch","template","this","throw","true","try","typedef",
        "typeid","typename","union","unsigned","using","virtual","void","volatile","while",
        "include","define","undef","ifdef","ifndef","endif","pragma","error","warning",
        "Q_OBJECT","Q_PROPERTY","Q_INVOKABLE","Q_SIGNAL","Q_SLOT","Q_ENUM","Q_FLAG",
        "signals","slots","emit","foreach","forever","Q_DECLARE_METATYPE","Q_DECLARE_INTERFACE"
    };
    
    for (const QString &kw : keywords) {
        QRegularExpression re(QString("\\b%1\\b").arg(QRegularExpression::escape(kw)));
        h.replace(re, QString("<span style='color:#569cd6;'>%1</span>").arg(kw));
    }
    
    // پیش‌پردازنده‌ها
    QRegularExpression preprocessorRe(R"(^(\s*)(#[A-Za-z_][A-Za-z0-9_]*))", 
                                       QRegularExpression::MultilineOption);
    h.replace(preprocessorRe, "\\1<span style='color:#c586c0;'>\\2</span>");
    
    // کامنت‌ها
    QRegularExpression singleCommentRe(R"(//[^\n]*)");
    h.replace(singleCommentRe, "<span style='color:#6a9955;'>\\0</span>");
    
    QRegularExpression multiCommentRe(R"(/\*[^*]*\*+(?:[^/*][^*]*\*+)*/)");
    h.replace(multiCommentRe, "<span style='color:#6a9955;'>\\0</span>");
    
    // رشته‌ها
    QRegularExpression stringRe(R"("([^"\\]|\\.)*")");
    h.replace(stringRe, "<span style='color:#ce9178;'>\\0</span>");
    
    // کاراکترها
    QRegularExpression charRe(R"('([^'\\]|\\.)*')");
    h.replace(charRe, "<span style='color:#ce9178;'>\\0</span>");
    
    // اعداد
    QRegularExpression numberRe(R"(\b(\d+\.?\d*(?:[eE][+-]?\d+)?)\b)");
    h.replace(numberRe, "<span style='color:#b5cea8;'>\\1</span>");
    
    return h;
}

// ─── تابع markdownToHtml (اصلاح‌شده) ──────────────────────
static QString markdownToHtml(const QString &text) {
    QString result = text;
    
    // پردازش بلاک‌های کد
    QRegularExpression codeBlockRe(R"(```(\w*)\n([\s\S]*?)```)");
    QRegularExpressionMatchIterator it = codeBlockRe.globalMatch(result);
    
    QList<QPair<int, QString>> replacements;
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString lang = match.captured(1).trimmed();
        QString code = match.captured(2);
        QString highlighted = highlightCpp(code);
        
        QString html = QString(
            "<pre style='background:#1e1e1e; padding:10px; "
            "border-radius:4px; font-family:Consolas,monospace; font-size:13px; "
            "overflow-x:auto; color:#d4d4d4;'>%1</pre>"
        ).arg(highlighted);
        
        replacements.append(qMakePair(match.capturedStart(), html));
    }
    
    // جایگزینی از انتها
    std::sort(replacements.begin(), replacements.end(), 
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) {
                  return a.first > b.first;
              });
    
    for (const auto &rep : replacements) {
        int start = rep.first;
        int end = result.indexOf("```", start + 3);
        if (end > 0) {
            end = result.indexOf("```", end + 3);
            if (end > 0) {
                end += 3;
                result.replace(start, end - start, rep.second);
            }
        }
    }
    
    // اینلاین کد
    QRegularExpression inlineCodeRe(R"(`([^`]+)`)");
    result.replace(inlineCodeRe, 
                   "<code style='background:#2d2d2d; color:#ce9178; padding:2px 6px; "
                   "border-radius:3px; font-family:Consolas,monospace; font-size:13px;'>\\1</code>");
    
    // لینک‌ها
    QRegularExpression linkRe(R"(\[([^\]]+)\]\(([^)]+)\))");
    result.replace(linkRe, "<a href='\\2' style='color:#569cd6;'>\\1</a>");
    
    // newline به br
    result.replace("\n", "<br>");
    
    return result;
}


// ── Orchestrator Slots ────────────────────────────────────

void MainWindow::onOrchestratorResponse(const QString &text) {
    appendChat("AI", text, m_isDark ? "#98c379" : "#2e7d32");
}

void MainWindow::onOrchestratorPlan(const QString &plan) {
    int modeId = m_modeGroup->checkedId();
    // Only Plan mode (id==1) switches to the Plan tab
    if (modeId == 1) {
        m_bottomStack->setCurrentIndex(0);
        QString planBg  = m_isDark ? "#1a1a2e" : "#f5f5f5";
        QString planFg  = m_isDark ? "#d0d0e4" : "#333333";
        m_planDisplay->setHtml(
            "<div style='color: #6c63ff; font-weight: bold; margin-bottom: 8px;'>"
            "Architecture Plan</div>"
            "<pre style='background-color: " + planBg + "; color: " + planFg + "; padding: 8px; "
                                              "border-left: 3px solid #6c63ff; font-family: Consolas; font-size: 14px;'>" +
            plan.toHtmlEscaped() + "</pre>");
    }
    // Chat (0) and Agent (2) show plan inline without switching tabs
    QString label = (modeId == 2) ? "Agent" : "Planner";
    QString color = (modeId == 2) ? (m_isDark ? "#c678dd" : "#7b1fa2") : (m_isDark ? "#6c63ff" : "#2e7d32");
    appendChat(label, QString("### Plan\n\n%1").arg(plan), color);
}

void MainWindow::onOrchestratorAgent(const QString &text) {
    m_bottomStack->setCurrentIndex(2);
    appendChat("Agent", text, m_isDark ? "#98c379" : "#2e7d32");
}

void MainWindow::onOrchestratorLog(const QString &message) {
    LOG_INFO("Pipeline", message);
    if (m_modelLogDisplay) {
        QString c = m_isDark ? "#61afef" : "#1565c0";
        m_modelLogDisplay->append(
            "<span style='color: " + c + "; font-weight: bold;'>[Pipeline] " + message.toHtmlEscaped() + "</span>");
    }
}

void MainWindow::onOrchestratorError(const QString &error) {
    LOG_ERROR("Pipeline", error);
    if (m_modelLogDisplay) {
        QString c = m_isDark ? "#e06c75" : "#c62828";
        m_modelLogDisplay->append(
            "<span style='color: " + c + "; font-weight: bold;'>[ERROR] " + error.toHtmlEscaped() + "</span>");
    }
    appendChat("System", "Error: " + error, "#e06c75");
}

void MainWindow::onAgentStageUpdate(const QString &stage, const QString &detail) {
    appendChat(stage, detail, m_isDark ? "#c678dd" : "#9c27b0");
    if (m_modelLogDisplay) {
        QString c = m_isDark ? "#c678dd" : "#9c27b0";
        m_modelLogDisplay->append(
            "<span style='color: " + c + "; font-weight: bold;'>[Pipeline] " + stage.toHtmlEscaped() + ": " + detail.toHtmlEscaped() + "</span>");
    }
}

void MainWindow::onPatchApprovalRequested(const QString &patchJson) {
    QJsonDocument doc = QJsonDocument::fromJson(patchJson.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        QMessageBox::warning(this, "Invalid Patch", "The generated patch could not be parsed as valid JSON.");
        m_orchestrator->rejectPendingPatch();
        return;
    }

    QJsonObject obj = doc.object();
    QString filePath = obj.value("file").toString();
    QString language = obj.value("language").toString();
    QString reason = obj.value("reason").toString();
    QJsonArray changes = obj.value("changes").toArray();

    QDialog dialog(this);
    dialog.setWindowFlags(Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);
    dialog.setWindowTitle("Modification Approval Required");

    auto *layout = new QVBoxLayout(&dialog);

    auto *title = new QLabel("Modification detected — approval required", &dialog);
    title->setStyleSheet("font-weight: bold; font-size: 14px; color: #e5c07b;");
    layout->addWidget(title);

    layout->addWidget(new QLabel(QString("File: <b>%1</b>").arg(filePath.toHtmlEscaped()), &dialog));
    layout->addWidget(new QLabel(QString("Language: %1").arg(language.toHtmlEscaped()), &dialog));
    if (!reason.isEmpty())
        layout->addWidget(new QLabel(QString("Reason: %1").arg(reason.toHtmlEscaped()), &dialog));

    layout->addWidget(new QLabel("Changes:", &dialog));

    for (int i = 0; i < changes.size(); ++i) {
        QJsonObject change = changes.at(i).toObject();
        int startLine = change.value("start_line").toInt();
        int endLine = change.value("end_line").toInt();
        QString oldCode = change.value("old_code").toString();
        QString newCode = change.value("new_code").toString();

        auto *gb = new QGroupBox(QString("Change %1 (lines %2-%3)").arg(i + 1).arg(startLine).arg(endLine), &dialog);
        auto *gbLayout = new QVBoxLayout(gb);

        auto *oldLabel = new QLabel("OLD:", &dialog);
        oldLabel->setStyleSheet("color: #e06c75; font-weight: bold;");
        auto *oldEdit = new QTextEdit(oldCode, &dialog);
        oldEdit->setReadOnly(true);
        oldEdit->setMaximumHeight(80);
        oldEdit->setStyleSheet("background: #3e0000; color: #e06c75; font-family: monospace;");

        auto *newLabel = new QLabel("NEW:", &dialog);
        newLabel->setStyleSheet("color: #98c379; font-weight: bold;");
        auto *newEdit = new QTextEdit(newCode, &dialog);
        newEdit->setReadOnly(true);
        newEdit->setMaximumHeight(80);
        newEdit->setStyleSheet("background: #003e00; color: #98c379; font-family: monospace;");

        gbLayout->addWidget(oldLabel);
        gbLayout->addWidget(oldEdit);
        gbLayout->addWidget(newLabel);
        gbLayout->addWidget(newEdit);
        layout->addWidget(gb);
    }

    auto *btnLayout = new QHBoxLayout;
    auto *yesBtn = new QPushButton("YES — Apply", &dialog);
    yesBtn->setStyleSheet("background: #98c379; color: #fff; font-weight: bold; padding: 8px 16px;");
    auto *noBtn = new QPushButton("NO — Cancel", &dialog);
    noBtn->setStyleSheet("background: #e06c75; color: #fff; font-weight: bold; padding: 8px 16px;");

    btnLayout->addWidget(yesBtn);
    btnLayout->addWidget(noBtn);
    layout->addLayout(btnLayout);

    QObject::connect(yesBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.accept();
        m_orchestrator->applyPendingPatch();
    });
    QObject::connect(noBtn, &QPushButton::clicked, &dialog, [this, &dialog]() {
        dialog.reject();
        m_orchestrator->rejectPendingPatch();
    });

    dialog.exec();
}

// ── Phase 3: Build / Run / Git / Terminal ──────────────────

void MainWindow::onBuildClicked() {
    // Try to detect project root
    QString buildDir;
    QString buildCmd = "make";
    QStringList buildArgs;

    QString workspace = m_currentBuildDir.isEmpty() ?
                            QApplication::applicationDirPath() : m_currentBuildDir;

    // Check for CMakeLists.txt
    QDir dir(workspace);
    if (QFile::exists(dir.filePath("CMakeLists.txt"))) {
        buildDir = dir.absolutePath();
        buildCmd = "cmake";
        buildArgs = QStringList{"--build", "."};
    } else if (QFile::exists(dir.filePath("Makefile"))) {
        buildDir = dir.absolutePath();
    } else if (QFile::exists(dir.filePath("QtOrchestrator.pro"))) {
        buildDir = dir.absolutePath();
        buildCmd = "qmake";
        buildArgs = QStringList{"QtOrchestrator.pro"};
    }

    if (buildDir.isEmpty()) {
        QFileDialog dlg(this, "Select build directory", workspace);
        dlg.setOption(QFileDialog::DontUseNativeDialog);
        dlg.setFileMode(QFileDialog::Directory);
        if (dlg.exec() != QDialog::Accepted) return;
        buildDir = dlg.selectedFiles().first();
    }

    m_buildPipeline->setBuildDir(buildDir);
    m_buildPipeline->setBuildCommand(buildCmd, buildArgs);

    m_terminalOutput->append(QString("<span style='color:#61afef;'>[Build] Starting in %1...</span>")
                                 .arg(buildDir.toHtmlEscaped()));
    m_buildStatusLabel->setText("Building...");
    m_buildStatusLabel->setStyleSheet("color: #e5c07b; font-size: 11px;");

    // Switch to terminal tab
    m_bottomTabs->setCurrentIndex(2);
    m_buildPipeline->build();
}

void MainWindow::onRunClicked(const QString &target) {
    Q_UNUSED(target)
    QString workspace = m_currentBuildDir.isEmpty() ?
                            QApplication::applicationDirPath() : m_currentBuildDir;

    // Search for binary in workspace and common build subdirectories
    QStringList searchPaths;
    searchPaths << workspace;
    searchPaths << workspace + "/build";
    searchPaths << workspace + "/build-Desktop-Debug";
    searchPaths << workspace + "/cmake-build-debug";
    searchPaths << workspace + "/cmake-build-release";
    searchPaths << workspace + "/out";

    QString execPath;
    for (const QString &base : searchPaths) {
        QString candidate = base + "/QtAiOrchestrator";
        if (QFile::exists(candidate)) {
            execPath = candidate;
            break;
        }
    }

    if (execPath.isEmpty()) {
        m_terminalOutput->append(
            "<span style='color:#e06c75;'>[Run] Binary not found. Build first.</span>");
        m_bottomTabs->setCurrentIndex(2);
        return;
    }

    m_terminalOutput->append(
        QString("<span style='color:#61afef;'>[Run] Executing: %1</span>")
            .arg(execPath.toHtmlEscaped()));
    m_bottomTabs->setCurrentIndex(2);
    m_terminalExecutor->execute(execPath, {}, {}, 0);
}

void MainWindow::onGitClicked() {
    QString repoPath = m_currentBuildDir.isEmpty() ?
                           QApplication::applicationDirPath() : m_currentBuildDir;
    m_gitManager->setRepoPath(repoPath);
    m_gitPanel->setRepoPath(repoPath);

    m_terminalOutput->append(
        QString("<span style='color:#61afef;'>[Git] Status in %1</span>")
            .arg(repoPath.toHtmlEscaped()));
    m_leftTabs->setCurrentIndex(m_leftTabs->count() - 1);
    m_bottomTabs->setCurrentIndex(2);
}

void MainWindow::onTerminalCommand() {
    QString cmd = m_terminalInput->text().trimmed();
    if (cmd.isEmpty()) return;

    m_terminalInput->clear();

    // Handle cd command locally
    if (cmd.startsWith("cd ")) {
        QString newDir = cmd.mid(3).trimmed();
        // Remove surrounding quotes if present
        if ((newDir.startsWith("\"") && newDir.endsWith("\"")) ||
            (newDir.startsWith("'") && newDir.endsWith("'")))
            newDir = newDir.mid(1, newDir.length() - 2);
        QDir dir(newDir);
        QString target = dir.isAbsolute() ? newDir : QDir(m_currentBuildDir.isEmpty() ?
                                                              QApplication::applicationDirPath() : m_currentBuildDir).absoluteFilePath(newDir);
        target = QDir::cleanPath(target);
        if (QDir(target).exists()) {
            m_currentBuildDir = target;
            m_terminalCwdLabel->setText(m_currentBuildDir);
            m_terminalOutput->append(
                QString("<span style='color:#98c379;'>$ cd %1</span>").arg(newDir.toHtmlEscaped()));
        } else {
            m_terminalOutput->append(
                QString("<span style='color:#e06c75;'>cd: %1: No such directory</span>").arg(newDir.toHtmlEscaped()));
        }
        return;
    }

    m_terminalOutput->append(
        QString("<span style='color:#98c379;'>$ %1</span>").arg(cmd.toHtmlEscaped()));
    m_terminalSendBtn->setEnabled(false);

    QString workDir = m_currentBuildDir.isEmpty() ?
                          QApplication::applicationDirPath() : m_currentBuildDir;
    m_terminalExecutor->executeShell(cmd, workDir);
}

void MainWindow::onBuildDone(int id, const BuildPipeline::BuildResult &result) {
    Q_UNUSED(id)
    if (result.success) {
        m_buildStatusLabel->setText("Build OK");
        m_buildStatusLabel->setStyleSheet("color: #98c379; font-size: 11px;");
        m_terminalOutput->append("<span style='color:#98c379;'>[Build] SUCCESS</span>");
    } else {
        m_buildStatusLabel->setText("Build FAILED");
        m_buildStatusLabel->setStyleSheet("color: #e06c75; font-size: 11px;");
        m_terminalOutput->append("<span style='color:#e06c75;'>[Build] FAILED</span>");

        if (!result.errors.isEmpty()) {
            m_terminalOutput->append("<span style='color:#e06c75;'>Errors:</span>");
            for (const QString &e : result.errors) {
                m_terminalOutput->append("  " + e);
            }
        }

        if (result.suggestions.isEmpty()) {
            m_terminalOutput->append(
                "<span style='color:#e5c07b;'>[Build] Asking Expert for fix suggestions...</span>");
            m_orchestrator->processQuery(
                "Fix these build errors:\n" + result.errors.join("\n"),
                Orchestrator::Mode::Agent);
        }
    }
}

// ── Phase 4: System Tray ───────────────────────────────────

void MainWindow::initTray() {
    connect(m_systemTray, &SystemTray::showWindowRequested, this, [this]() {
        showNormal();
        activateWindow();
        raise();
    });
    connect(m_systemTray, &SystemTray::hideWindowRequested, this, [this]() {
        hide();
    });
    connect(m_systemTray, &SystemTray::quitRequested, this, [this]() {
        saveSession();
        m_settingsManager->setWindowGeometry(saveGeometry());
        m_settingsManager->setWindowState(saveState());
        m_settingsManager->setMainSplitterPos(m_mainSplitter->sizes().value(0, 220));
        m_settingsManager->save();
        m_processManager->terminateAll();
        QApplication::quit();
    });
    connect(m_systemTray, &SystemTray::openSettingsRequested, this, &MainWindow::onOpenSettings);

    if (m_settingsManager->minimizeToTray()) {
        m_systemTray->show();
        m_systemTray->showNotification("Qt AI Orchestrator",
                                       "Application minimized to tray. Models continue running.");
    }
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_settingsManager->minimizeToTray()) {
        hide();
        m_systemTray->showNotification("Qt AI Orchestrator",
                                       "Still running in system tray. Double-click to restore.");
        event->ignore();
    } else {
        saveSession();
        m_settingsManager->setWindowGeometry(saveGeometry());
        m_settingsManager->setWindowState(saveState());
        m_settingsManager->save();
        m_processManager->terminateAll();
        event->accept();
    }
}

// ── Phase 4: Session Persistence ───────────────────────────

void MainWindow::initSessionPersistence() {
    // Load last session if exists
    QString lastFile = m_settingsManager->lastConversationFile();
    if (!lastFile.isEmpty()) {
        QFileInfo fi(lastFile);
        if (fi.exists()) {
            auto entries = m_sessionManager->loadConversation(fi.fileName());
            for (const auto &e : entries) {
                QString color = (e.role == "user") ? (m_isDark ? "#61afef" : "#1565c0") :
                                    (e.role == "system") ? "#e06c75" : (m_isDark ? "#98c379" : "#2e7d32");
                appendChat(e.role, e.text, color);
            }
            LOG_INFO("Session", QString("Restored %1 conversation entries").arg(entries.size()));
        }
    }
}

void MainWindow::saveSession() {
    QList<ConversationEntry> entries;

    // Collect chat entries
    auto collectFrom = [](QTextEdit *display, const QString &mode) -> QList<ConversationEntry> {
        QList<ConversationEntry> result;
        if (!display) return result;
        QString html = display->toHtml();
        // Simple heuristic: parse <span> role tags from appendChat format
        QStringList lines = display->toPlainText().split('\n');
        for (const QString &line : lines) {
            if (line.trimmed().isEmpty()) continue;
            ConversationEntry e;
            e.role = line.startsWith("User:") ? "user" :
                         line.startsWith("AI:") ? "ai" :
                         line.startsWith("System:") ? "system" : "ai";
            e.text = line.section(':', 1).trimmed();
            e.mode = mode;
            e.timestamp = QDateTime::currentSecsSinceEpoch();
            result << e;
        }
        return result;
    };

    entries << collectFrom(m_chatDisplay, "chat");
    entries << collectFrom(m_planDisplay, "plan");
    entries << collectFrom(m_agentDisplay, "agent");

    if (entries.isEmpty()) return;

    QString filename = SessionManager::timestampFilename();
    if (m_sessionManager->saveConversation(filename, entries)) {
        m_settingsManager->setLastConversationFile(filename);
        m_settingsManager->save();
    }
}

void MainWindow::loadSession() {
    QStringList sessions = m_sessionManager->listSessions();
    if (sessions.isEmpty()) {
        LOG_INFO("Session", "No saved sessions found");
        return;
    }

    // Load most recent session
    QString latest = sessions.first();
    QFileInfo fi(latest);
    auto entries = m_sessionManager->loadConversation(fi.fileName());

    m_chatDisplay->clear();
    m_planDisplay->clear();
    m_agentDisplay->clear();

    for (const auto &e : entries) {
        QString color = (e.role == "user") ? (m_isDark ? "#61afef" : "#1565c0") :
                            (e.role == "system") ? "#e06c75" : (m_isDark ? "#98c379" : "#2e7d32");
        appendChat(e.role, e.text, color);
    }

    m_settingsManager->setLastConversationFile(fi.fileName());
    LOG_INFO("Session", QString("Loaded session: %1 (%2 entries)")
                            .arg(fi.fileName()).arg(entries.size()));
}

// ── Phase 4: Settings ──────────────────────────────────────

void MainWindow::onOpenSettings() {
    auto *dialog = new SettingsDialog(m_settingsManager, m_isDark, this);
    connect(dialog, &SettingsDialog::settingsApplied, this, [this]() {
        // Re-register models so VramManager uses latest settings
        m_portToKey.clear();
        QString serverPath = m_settingsManager->llamaServerPath();
        QString modelsDir = m_settingsManager->modelsDirectory();
        for (const QString &key : m_settingsManager->modelKeys()) {
            auto cfg = m_settingsManager->modelConfig(key);
            QString modelPath = modelsDir + "/" + cfg.modelFile;
            QString mmprojPath = cfg.mmprojFile.isEmpty() ? "" : modelsDir + "/" + cfg.mmprojFile;
            ModelInfo info;
            info.name = cfg.name;
            info.serverPath = serverPath;
            info.modelPath = modelPath;
            info.mmprojPath = mmprojPath;
            info.port = cfg.port;
            info.ngl = cfg.ngl;
            info.ctx = cfg.ctx;
            info.isVision = cfg.isVision;
            info.isEmbedding = cfg.isEmbedding;
            info.estimatedVRAM_MB = cfg.vramMB;
            info.schedule = "on-demand";
            info.batchSize = cfg.batchSize;
            info.ubatchSize = cfg.ubatchSize;
            info.temperature = cfg.temperature;
            m_vramManager->registerModel(key, info);
            m_portToKey[cfg.port] = key;
            LOG_INFO("MainWindow", QString("Re-registered %1: %2").arg(key, cfg.modelFile));
        }
        // Apply theme change
        if (m_settingsManager->theme() == "light" && m_isDark) {
            m_isDark = false;
            applyTheme();
        } else if (m_settingsManager->theme() == "dark" && !m_isDark) {
            m_isDark = true;
            applyTheme();
        }
        // Apply minimize-to-tray setting
        if (m_settingsManager->minimizeToTray()) {
            if (!m_systemTray->isVisible())
                m_systemTray->show();
        } else {
            if (m_systemTray->isVisible())
                m_systemTray->hide();
        }
        // Apply notification setting (re-show tray if needed to apply)
        if (m_settingsManager->notificationsEnabled() && m_systemTray->isVisible()) {
            m_systemTray->showNotification("Settings Updated",
                                           "New settings have been applied.");
        }
        // Update VRAM limit from settings
        m_vramManager->setTotalVRAM(m_settingsManager->vramLimitMB());
        // Re-register orchestrator ports
        m_orchestrator->setPlannerPort(m_settingsManager->modelPort("planner"));
        m_orchestrator->setCoderPort(m_settingsManager->modelPort("coder"));
        m_orchestrator->setExpertPort(m_settingsManager->modelPort("expert"));
        m_orchestrator->setVisionPort(m_settingsManager->modelPort("vision"));
        m_orchestrator->setEmbedPort(m_settingsManager->modelPort("embed"));
        LOG_INFO("Settings", "Settings applied");
    });
    dialog->exec();
    dialog->deleteLater();
}

void MainWindow::onSaveSession() {
    saveSession();
    m_systemTray->showNotification("Session Saved",
                                   "Conversation saved successfully.");
}

void MainWindow::onLoadSession() {
    loadSession();
    m_systemTray->showNotification("Session Loaded",
                                   "Previous conversation restored.");
}

void MainWindow::onOpenFolder() {
    QFileDialog dlg(this, "Open Folder",
                    m_currentBuildDir.isEmpty() ? QApplication::applicationDirPath() : m_currentBuildDir);
    dlg.setOption(QFileDialog::DontUseNativeDialog);
    dlg.setFileMode(QFileDialog::Directory);
    if (dlg.exec() != QDialog::Accepted) return;
    QString dir = dlg.selectedFiles().first();
    {
        m_currentBuildDir = dir;
        m_terminalCwdLabel->setText(m_currentBuildDir);
        m_gitPanel->setRepoPath(m_currentBuildDir);
        m_gitManager->setRepoPath(m_currentBuildDir);
        LOG_INFO("File", "Opened folder: " + dir);
        m_terminalOutput->append(
            QString("<span style='color:#61afef;'>[Folder] Opened: %1</span>")
                .arg(dir.toHtmlEscaped()));
        m_bottomTabs->setCurrentIndex(2);
        // Navigate file tree to the opened folder
        m_fileTree->setRootIndex(m_fileSystemModel->setRootPath(dir));
        // Open project in Project tab
        m_projectExplorer->setRoot(dir);
        m_leftTabs->setCurrentIndex(1);
    }
}

void MainWindow::onSaveCurrentFile() {
    CodeEditor *ed = activeCodeEditor();
    if (!ed) return;
    if (ed->filePath().isEmpty()) {
        onSaveCurrentFileAs();
        return;
    }
    if (ed->saveFile()) {
        LOG_INFO("File", "Saved: " + ed->filePath());
        m_terminalOutput->append(
            QString("<span style='color:#98c379;'>[File] Saved: %1</span>")
                .arg(ed->filePath().toHtmlEscaped()));
    } else {
        LOG_ERROR("File", "Failed to save: " + ed->filePath());
    }
}

void MainWindow::onSaveCurrentFileAs() {
    CodeEditor *ed = activeCodeEditor();
    if (!ed) return;
    if (ed->saveAsFile()) {
        LOG_INFO("File", "Saved as: " + ed->filePath());
        m_terminalOutput->append(
            QString("<span style='color:#98c379;'>[File] Saved as: %1</span>")
                .arg(ed->filePath().toHtmlEscaped()));
    }
}

void MainWindow::onAutoSaveCode() {
    for (int i = 0; i < m_editorSplitter->count(); ++i) {
        auto *ed = qobject_cast<CodeEditor *>(m_editorSplitter->widget(i));
        if (ed && ed->isModified() && !ed->filePath().isEmpty()) {
            ed->saveFile();
            LOG_INFO("File", "Auto-saved: " + ed->filePath());
        }
    }
}

void MainWindow::onStartAllModels() {
    startAllModels();
}

CodeEditor *MainWindow::activeCodeEditor() const {
    return m_activeEditor ? m_activeEditor : m_codeEditor;
}

void MainWindow::onMonitorUpdate() {
    if (m_monitorWidget)
        m_monitorWidget->refresh();
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event); // اجازه بده Qt کار خودش را انجام دهد

    // کارهای خودمان هنگام باز شدن پنجره:
    if (m_orchestrator && m_settingsManager) {
        m_orchestrator->setPlannerPort(m_settingsManager->modelPort("planner"));
        m_orchestrator->setCoderPort(m_settingsManager->modelPort("coder"));
        m_orchestrator->setExpertPort(m_settingsManager->modelPort("expert"));
        m_orchestrator->setVisionPort(m_settingsManager->modelPort("vision"));
        m_orchestrator->setEmbedPort(m_settingsManager->modelPort("embed"));
    }

    LOG_INFO("UI", "Main Window shown and ports synchronized.");
}
