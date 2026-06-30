#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QTimer>
#include <QTabWidget>
#include <QStackedWidget>
#include <QPlainTextEdit>
#include <QTreeView>
#include <QFileSystemModel>
#include <QFileInfo>
#include <QScrollBar>
#include <QImage>
#include <QPixmap>
#include <QFileDialog>
#include <QMessageBox>
#include <QList>
#include <QMap>
#include <QButtonGroup>
#include <QRadioButton>
#include <QMenuBar>
#include <QCloseEvent>
#include <QProcess>

#include "logger.h"
#include "process_manager.h"
#include "api_client.h"
#include "vram_manager.h"
#include "settings_manager.h"
#include "session_manager.h"
#include "orchestrator.h"
#include "build_pipeline.h"
#include "terminal_executor.h"
#include "git_manager.h"
#include "system_tray.h"
#include "settings_dialog.h"
#include "monitor_widget.h"
#include "git_panel.h"
#include "CodeEditor.h"
#include "ProjectExplorer.h"

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    static constexpr int PLANNER_PORT    = 8001;
    static constexpr int CODER_PORT      = 8002;
    static constexpr int EXPERT_PORT     = 8003;
    static constexpr int VISION_PORT     = 8004;
    static constexpr int EMBED_PORT      = 8005;

private slots:
    void onSendClicked();
    void onClearChat();
    void onImageQnA();
    void onPdfQnA();
    void onAttachImage();
    void onAttachPdf();
    void onRemovePdf();
    void onProgramClicked();
    void onPictureClicked();
    void onPdfViewerClicked();
    void onModeChanged(int id);
    void onVRAMUpdate();
    void onModelLog(const QString &source, const QString &message);
    void onModelStarted(int port, const QString &name);
    void onModelStopped(int port, const QString &name);
    void onOrchestratorResponse(const QString &text);
    void onOrchestratorPlan(const QString &plan);
    void onOrchestratorAgent(const QString &text);
    void onOrchestratorLog(const QString &message);
    void onOrchestratorError(const QString &error);
    void onAgentStageUpdate(const QString &stage, const QString &detail);
    void onPatchApprovalRequested(const QString &patchJson);
    void onBuildClicked();
    void onRunClicked(const QString &target = {});
    void onGitClicked();
    void onTerminalCommand();
    void onBuildDone(int id, const BuildPipeline::BuildResult &result);
    void onOpenSettings();
    void onSaveSession();
    void onLoadSession();
    void onStartAllModels();
    void onOpenFolder();
    void onSaveCurrentFile();
    void onSaveCurrentFileAs();
    void onAutoSaveCode();
    void onMonitorUpdate();

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void setupUI();
    void applyTheme();
    void createMenuBar();
    void createRunMenu(QMenuBar *mb);
    void createTopBar(QVBoxLayout *mainLayout);
    void createLeftPanel(QSplitter *mainSplitter);
    void createCenterPanel(QSplitter *mainSplitter);
    void createBottomPanel();
    void createInputBar(QVBoxLayout *centerLayout);

    void handleNormalSend(const QString &query);
    void handleProgramSend(const QString &query);
    void handlePictureSend(const QString &query);
    void handlePdfViewerSend(const QString &query);

    void startAllModels();
    void startNextInQueue();
    void advanceStartAllQueue(int port);
    void startPlanner();
    void startEmbed();
    void startCoder();
    void startExpert();
    void startVision();
    void stopAllModels();

    ModelConfig modelConfigFromSettings(const QString &key) const;
    int modelPort(const QString &key) const;
    void appendChat(const QString &role, const QString &text, const QString &color);
    void refreshPdfList();
    void initTray();
    void initSessionPersistence();
    void saveSession();
    void loadSession();
    void updateModelStatusUI();
    void refreshVRAMAsync();
    CodeEditor *activeCodeEditor() const;

    // ─── Core Components ───
    ProcessManager *m_processManager = nullptr;
    ApiClient *m_apiClient = nullptr;
    Orchestrator *m_orchestrator = nullptr;
    VramManager *m_vramManager = nullptr;
    SettingsManager *m_settingsManager = nullptr;
    SessionManager *m_sessionManager = nullptr;
    SystemTray *m_systemTray = nullptr;

    // ─── Tools ───
    TerminalExecutor *m_terminalExecutor = nullptr;
    GitManager *m_gitManager = nullptr;
    BuildPipeline *m_buildPipeline = nullptr;

    // ─── UI Components ───
    QSplitter *m_mainSplitter = nullptr;
    QSplitter *m_centerSplitter = nullptr;
    QSplitter *m_editorSplitter = nullptr;
    QWidget *m_topBar = nullptr;
    QTabWidget *m_leftTabs = nullptr;
    QTabWidget *m_bottomTabs = nullptr;
    QStackedWidget *m_bottomStack = nullptr;
    QWidget *m_inputBar = nullptr;

    // ─── Top Bar ───
    QLabel *m_appIcon = nullptr;
    QLabel *m_vramLabel = nullptr;
    QButtonGroup *m_modeGroup = nullptr;
    QPushButton *m_themeBtn = nullptr;

    // ─── Chat / Plan / Agent Display ───
    ChatEditor *m_chatDisplay = nullptr;
    ChatEditor *m_planDisplay = nullptr;
    ChatEditor *m_agentDisplay = nullptr;
    ChatEditor *m_modelLogDisplay = nullptr;
    ChatEditor *m_terminalOutput = nullptr;

    // ─── Input Bar ───
    QLineEdit *m_userInput = nullptr;
    QPushButton *m_sendBtn = nullptr;
    QPushButton *m_clearChatBtn = nullptr;
    QPushButton *m_imageBtn = nullptr;
    QPushButton *m_pdfBtn = nullptr;
    QPushButton *m_programBtn = nullptr;
    QPushButton *m_pictureBtn = nullptr;
    QPushButton *m_pdfViewerBtn = nullptr;
    QLabel *m_imagePreview = nullptr;

    // ─── Workflow Mode ───
    enum class WorkflowMode { None, Program, Picture, PdfViewer };
    WorkflowMode m_workflowMode = WorkflowMode::None;

    // ─── Terminal ───
    QLineEdit *m_terminalInput = nullptr;
    QPushButton *m_terminalSendBtn = nullptr;
    QLabel *m_buildStatusLabel = nullptr;
    QLabel *m_terminalCwdLabel = nullptr;

    // ─── Left Panel ───
    QTreeView *m_fileTree = nullptr;
    QFileSystemModel *m_fileSystemModel = nullptr;
    ProjectExplorer *m_projectExplorer = nullptr;
    GitPanel *m_gitPanel = nullptr;
    MonitorWidget *m_monitorWidget = nullptr;

    // ─── Model Status ───
    QMap<QString, QLabel*> m_modelStatusDots;
    QLabel *m_healthLabel = nullptr;
    QLabel *m_vramDetailLabel = nullptr;
    QPushButton *m_startAllBtn = nullptr;
    QPushButton *m_stopAllBtn = nullptr;
    QPushButton *m_startPlannerBtn = nullptr;
    QPushButton *m_startEmbedBtn = nullptr;
    QPushButton *m_startCoderBtn = nullptr;
    QPushButton *m_startExpertBtn = nullptr;
    QPushButton *m_startVisionBtn = nullptr;

    // ─── PDF List ───
    QWidget *m_pdfListContainer = nullptr;
    QVBoxLayout *m_pdfListLayout = nullptr;

    // ─── Code Editor ───
    CodeEditor *m_codeEditor = nullptr;
    CodeEditor *m_activeEditor = nullptr;

    // ─── Timers ───
    QTimer *m_healthTimer = nullptr;
    QTimer *m_monitorTimer = nullptr;

    // ─── State ───
    bool m_isDark = true;
    bool m_vramBusy = false;
    QString m_currentBuildDir;
    QString m_attachedImagePath;
    QImage m_attachedImage;
    QStringList m_failedModels;
    QStringList m_startAllQueue;
    int m_startAllIndex = -1;
    QMap<int, QString> m_portToKey;
    QProcess *m_vramProcess = nullptr;
    QAction *m_startAllModelsAction = nullptr;
    QString m_selectedPdf;
};

#endif
