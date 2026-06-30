#include "settings_dialog.h"
#include "settings_manager.h"
#include "logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QTabWidget>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QApplication>
#include <QStyle>
#include <QShowEvent>  // از نسخه قدیمی

SettingsDialog::SettingsDialog(SettingsManager *settings, bool isDark, QWidget *parent)
    : QDialog(parent), m_settings(settings), m_isDark(isDark) {
    setWindowTitle("Settings");
    setMinimumSize(680, 650);
    setModal(true);

    applyStyle();
    setupUI();
    loadSettings();
}

// ===== از نسخه قدیمی: applyStyle کامل با رنگ‌های داینامیک =====
void SettingsDialog::applyStyle() {
    QString bg = m_isDark ? "#1e2229" : "#f5f5f5";
    QString fg = m_isDark ? "#abb2bf" : "#333333";
    QString border = m_isDark ? "#3e4452" : "#cccccc";
    QString hoverBg = m_isDark ? "#2c313a" : "#e8f0fe";
    QString selectedBg = m_isDark ? "#3e4452" : "#cce5ff";
    QString accent = m_isDark ? "#6c63ff" : "#1a73e8";

    setStyleSheet(QString(
                      "QDialog { background-color: %1; }"
                      "QGroupBox {"
                      "  font-weight: bold;"
                      "  border: 1px solid %2;"
                      "  border-radius: 4px;"
                      "  margin-top: 12px;"
                      "  padding-top: 14px;"
                      "  color: %3;"
                      "}"
                      "QGroupBox::title {"
                      "  subcontrol-origin: margin;"
                      "  left: 10px;"
                      "  padding: 0 4px;"
                      "  color: %3;"
                      "}"
                      "QLabel { color: %3; }"
                      "QLineEdit {"
                      "  background-color: %1;"
                      "  color: %3;"
                      "  border: 1px solid %2;"
                      "  border-radius: 3px;"
                      "  padding: 4px 6px;"
                      "}"
                      "QLineEdit:focus {"
                      "  border: 1px solid %4;"
                      "}"
                      "QSpinBox, QDoubleSpinBox {"
                      "  background-color: %1;"
                      "  color: %3;"
                      "  border: 1px solid %2;"
                      "  border-radius: 3px;"
                      "  padding: 2px 4px;"
                      "}"
                      "QSpinBox:focus, QDoubleSpinBox:focus {"
                      "  border: 1px solid %4;"
                      "}"
                      "QComboBox {"
                      "  background-color: %1;"
                      "  color: %3;"
                      "  border: 1px solid %2;"
                      "  border-radius: 3px;"
                      "  padding: 4px 8px;"
                      "}"
                      "QComboBox:hover {"
                      "  background-color: %5;"
                      "}"
                      "QComboBox::drop-down {"
                      "  border: none;"
                      "}"
                      "QComboBox QAbstractItemView {"
                      "  background-color: %1;"
                      "  color: %3;"
                      "  selection-background-color: %6;"
                      "}"
                      "QCheckBox {"
                      "  color: %3;"
                      "}"
                      "QCheckBox::indicator {"
                      "  width: 16px;"
                      "  height: 16px;"
                      "}"
                      "QPushButton {"
                      "  background-color: %5;"
                      "  color: %3;"
                      "  border: 1px solid %2;"
                      "  border-radius: 3px;"
                      "  padding: 6px 14px;"
                      "}"
                      "QPushButton:hover {"
                      "  background-color: %6;"
                      "  color: %4;"
                      "}"
                      "QPushButton:pressed {"
                      "  background-color: %4;"
                      "  color: white;"
                      "}"
                      "QPushButton:disabled {"
                      "  color: #5c6370;"
                      "  background-color: %1;"
                      "}"
                      "QTabWidget::pane {"
                      "  border: 1px solid %2;"
                      "  border-radius: 3px;"
                      "  background-color: %1;"
                      "}"
                      "QTabBar::tab {"
                      "  padding: 6px 16px;"
                      "  color: %3;"
                      "  background-color: %1;"
                      "  border: 1px solid %2;"
                      "  border-bottom: none;"
                      "  border-top-left-radius: 3px;"
                      "  border-top-right-radius: 3px;"
                      "}"
                      "QTabBar::tab:selected {"
                      "  background-color: %5;"
                      "  border-top: 2px solid %4;"
                      "}"
                      "QTabBar::tab:hover {"
                      "  background-color: %5;"
                      "}"
                      "QScrollArea {"
                      "  border: none;"
                      "  background-color: transparent;"
                      "}"
                      "QScrollBar:vertical {"
                      "  background-color: %1;"
                      "  width: 10px;"
                      "  border: none;"
                      "}"
                      "QScrollBar::handle:vertical {"
                      "  background-color: %5;"
                      "  border-radius: 5px;"
                      "  min-height: 20px;"
                      "}"
                      "QScrollBar::handle:vertical:hover {"
                      "  background-color: %6;"
                      "}"
                      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
                      "  height: 0px;"
                      "}"
                      "QDialogButtonBox QPushButton {"
                      "  padding: 6px 24px;"
                      "  min-width: 80px;"
                      "}"
                      "QDialogButtonBox QPushButton:default {"
                      "  background-color: %4;"
                      "  color: white;"
                      "  border-color: %4;"
                      "}"
                      "QDialogButtonBox QPushButton:default:hover {"
                      "  background-color: #1557b0;"
                      "}"
                      ).arg(bg, border, fg, accent, hoverBg, selectedBg));
}

// ===== از نسخه قدیمی: showEvent با بارگذاری مجدد =====
void SettingsDialog::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    loadSettings();
}

void SettingsDialog::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(8);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    auto *tabs = new QTabWidget();
    tabs->setStyleSheet("QTabWidget::pane { margin: -1px; }");

    // ── General tab ── از نسخه قدیمی
    tabs->addTab(createGeneralTab(), "General");

    // ── Models tab ── از نسخه قدیمی
    tabs->addTab(createModelsTab(), "Models");

    // ── Paths tab ── از نسخه قدیمی
    tabs->addTab(createPathsTab(), "Paths");

    mainLayout->addWidget(tabs, 1);

    // ── Buttons با Apply (از نسخه قدیمی) ──
    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
    buttonBox->button(QDialogButtonBox::Apply)->setText("Apply");

    connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
        saveSettings();
        accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        saveSettings();
        emit settingsApplied();
        QMessageBox::information(this, "Settings", "Settings saved successfully!");
    });

    mainLayout->addWidget(buttonBox);
}

// ===== از نسخه قدیمی: createGeneralTab =====
QWidget* SettingsDialog::createGeneralTab() {
    auto *widget = new QWidget();
    auto *layout = new QFormLayout(widget);
    layout->setSpacing(10);
    layout->setContentsMargins(10, 10, 10, 10);

    m_themeCombo = new QComboBox();
    m_themeCombo->addItems({"dark", "light"});
    m_themeCombo->setToolTip("Select the application theme.\nDark theme is recommended for better readability.");
    layout->addRow("Theme:", m_themeCombo);

    m_autoStartModels = new QCheckBox("Auto-start models on launch");
    m_autoStartModels->setToolTip("Automatically start all configured models when the application launches.");
    layout->addRow(m_autoStartModels);

    m_minimizeToTray = new QCheckBox("Minimize to system tray");
    m_minimizeToTray->setToolTip("When closing the window, minimize to system tray instead of exiting.");
    layout->addRow(m_minimizeToTray);

    m_notifications = new QCheckBox("Enable tray notifications");
    m_notifications->setToolTip("Show notifications for important events (model loading, errors, etc.)");
    layout->addRow(m_notifications);

    layout->addItem(new QSpacerItem(0, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));

    auto *resetBtn = new QPushButton("Reset All Settings to Defaults");
    resetBtn->setToolTip("Reset all settings to their default values.\nThis action cannot be undone.");
    resetBtn->setStyleSheet("QPushButton { color: #e06c75; }");
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, "Reset Settings",
                                  "Are you sure you want to reset all settings to defaults?\nThis action cannot be undone.",
                                  QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
            m_settings->resetToDefaults();
            loadSettings();
            QMessageBox::information(this, "Reset Complete", "All settings have been reset to defaults.");
        }
    });
    layout->addRow(resetBtn);

    return widget;
}

// ===== از نسخه قدیمی: createPathsTab =====
QWidget* SettingsDialog::createPathsTab() {
    auto *widget = new QWidget();
    auto *layout = new QFormLayout(widget);
    layout->setSpacing(10);
    layout->setContentsMargins(10, 10, 10, 10);

    auto *llamaRow = new QHBoxLayout();
    m_llamaPath = new QLineEdit();
    m_llamaPath->setPlaceholderText("/path/to/llama-server");
    m_llamaPath->setToolTip("Path to the llama-server executable.\nThis is required for running AI models.");

    auto *browseLlama = new QPushButton("Browse...");
    browseLlama->setToolTip("Browse for llama-server executable.");
    browseLlama->setFixedWidth(80);
    connect(browseLlama, &QPushButton::clicked, this, [this]() {
        QFileDialog fd(this, "Select llama-server binary", m_llamaPath->text());
        fd.setOption(QFileDialog::DontUseNativeDialog);
        fd.setFileMode(QFileDialog::ExistingFile);
        if (fd.exec() != QDialog::Accepted) return;
        QString p = fd.selectedFiles().first();
        if (!p.isEmpty()) m_llamaPath->setText(p);
    });
    llamaRow->addWidget(m_llamaPath, 1);
    llamaRow->addWidget(browseLlama);
    layout->addRow("llama-server:", llamaRow);

    auto *dirRow = new QHBoxLayout();
    m_modelsDir = new QLineEdit();
    m_modelsDir->setPlaceholderText("/path/to/Models/");
    m_modelsDir->setToolTip("Directory where model files (.gguf) are stored.");

    auto *browseDir = new QPushButton("Browse...");
    browseDir->setToolTip("Browse for models directory.");
    browseDir->setFixedWidth(80);
    connect(browseDir, &QPushButton::clicked, this, [this]() {
        QFileDialog fd(this, "Select Models Directory", m_modelsDir->text());
        fd.setOption(QFileDialog::DontUseNativeDialog);
        fd.setFileMode(QFileDialog::Directory);
        fd.setOption(QFileDialog::ShowDirsOnly);
        if (fd.exec() != QDialog::Accepted) return;
        QString d = fd.selectedFiles().first();
        if (!d.isEmpty()) m_modelsDir->setText(d);
    });
    dirRow->addWidget(m_modelsDir, 1);
    dirRow->addWidget(browseDir);
    layout->addRow("Models directory:", dirRow);

    m_vramLimit = new QSpinBox();
    m_vramLimit->setRange(4096, 65536);
    m_vramLimit->setSuffix(" MB");
    m_vramLimit->setSingleStep(1024);
    m_vramLimit->setToolTip("Maximum VRAM to allocate for AI models.\nThis prevents models from using too much memory.");
    layout->addRow("VRAM limit:", m_vramLimit);

    layout->addItem(new QSpacerItem(0, 20, QSizePolicy::Minimum, QSizePolicy::Expanding));

    auto *infoLabel = new QLabel("Note: Changes to paths require restarting the application to take effect.");
    infoLabel->setStyleSheet("color: #e5c07b; font-size: 11px;");
    infoLabel->setWordWrap(true);
    layout->addRow(infoLabel);

    return widget;
}

// ===== از نسخه قدیمی: createModelsTab با scroll area =====
QWidget* SettingsDialog::createModelsTab() {
    auto *widget = new QWidget();
    auto *layout = new QVBoxLayout(widget);
    layout->setSpacing(6);
    layout->setContentsMargins(0, 0, 0, 0);

    auto *infoLabel = new QLabel(
        "Configure each model's parameters. "
        "Models marked with '*' are vision-capable and require an MMProj file."
        );
    infoLabel->setStyleSheet("font-size: 11px; padding: 4px 8px;");
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);

    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background-color: transparent; }");

    auto *container = new QWidget();
    auto *containerLayout = new QVBoxLayout(container);
    containerLayout->setSpacing(8);
    containerLayout->setContentsMargins(4, 4, 4, 4);

    containerLayout->addWidget(createModelGroup("planner", "Planner (Gemma-4)", false, "Always-On"));
    containerLayout->addWidget(createModelGroup("embed", "Embed (Nomic)", false, "Always-On"));
    containerLayout->addWidget(createModelGroup("coder", "Coder (Qwen-14B)", false, "On-Demand"));
    containerLayout->addWidget(createModelGroup("expert", "Expert (Qwen-30B)", false, "On-Demand"));
    containerLayout->addWidget(createModelGroup("vision", "Vision (Gemma-4) *", true, "Temporary"));

    containerLayout->addStretch();

    scrollArea->setWidget(container);
    layout->addWidget(scrollArea, 1);

    return widget;
}

// ===== از نسخه قدیمی: createModelGroup کامل با schedule و hasMMProj =====
QWidget* SettingsDialog::createModelGroup(const QString &key, const QString &displayName,
                                          bool hasMMProj, const QString &schedule) {
    auto *group = new QGroupBox(displayName);
    group->setStyleSheet("QGroupBox { padding-top: 12px; margin-top: 8px; }");

    auto *layout = new QGridLayout(group);
    layout->setSpacing(6);
    layout->setContentsMargins(10, 14, 10, 10);

    ModelEditWidgets w;
    w.hasMMProj = hasMMProj;
    w.schedule = schedule;

    int row = 0;

    // Schedule label
    auto *scheduleLabel = new QLabel(QString("Schedule: %1").arg(schedule));
    scheduleLabel->setStyleSheet("font-size: 10px; color: #61afef; font-weight: bold;");
    layout->addWidget(scheduleLabel, row, 0, 1, 4, Qt::AlignRight);
    row++;

    // Model file
    auto *fileLabel = new QLabel("Model:");
    fileLabel->setStyleSheet("font-weight: bold; font-size: 11px;");

    auto *fileRow = new QHBoxLayout();
    w.modelFile = new QLineEdit();
    w.modelFile->setPlaceholderText("model.gguf");
    w.modelFile->setToolTip("Path to the model file (.gguf format).");

    w.browseBtn = new QPushButton("...");
    w.browseBtn->setToolTip("Browse for model file.");
    w.browseBtn->setFixedSize(28, 28);
    w.browseBtn->setStyleSheet("font-weight: bold; font-size: 14px;");
    connect(w.browseBtn, &QPushButton::clicked, this, [this, key]() {
        QString dir = m_settings->modelsDirectory();
        QFileDialog fd(this, "Select Model File", dir, "GGUF (*.gguf);;All Files (*)");
        fd.setOption(QFileDialog::DontUseNativeDialog);
        fd.setFileMode(QFileDialog::ExistingFile);
        if (fd.exec() != QDialog::Accepted) return;
        QString path = fd.selectedFiles().first();
        if (!path.isEmpty()) {
            auto &mw = m_modelWidgets[key];
            if (path.startsWith(dir))
                mw.modelFile->setText(path.mid(dir.length() + 1));
            else
                mw.modelFile->setText(path);
        }
    });
    fileRow->addWidget(w.modelFile, 1);
    fileRow->addWidget(w.browseBtn);
    layout->addWidget(fileLabel, row, 0, Qt::AlignTop);
    layout->addLayout(fileRow, row, 1, 1, 3);
    row++;

    // MMProj file (vision only)
    if (hasMMProj) {
        auto *mmprojLabel = new QLabel("MMProj:");
        mmprojLabel->setStyleSheet("font-weight: bold; font-size: 11px;");

        auto *mmprojRow = new QHBoxLayout();
        w.mmprojFile = new QLineEdit();
        w.mmprojFile->setPlaceholderText("mmproj.gguf (optional)");
        w.mmprojFile->setToolTip("MMProj file for vision models (optional).");

        w.mmprojBrowseBtn = new QPushButton("...");
        w.mmprojBrowseBtn->setToolTip("Browse for MMProj file.");
        w.mmprojBrowseBtn->setFixedSize(28, 28);
        w.mmprojBrowseBtn->setStyleSheet("font-weight: bold; font-size: 14px;");
        connect(w.mmprojBrowseBtn, &QPushButton::clicked, this, [this, key]() {
            QString dir = m_settings->modelsDirectory();
            QFileDialog fd(this, "Select MMProj File", dir, "GGUF (*.gguf);;All Files (*)");
            fd.setOption(QFileDialog::DontUseNativeDialog);
            fd.setFileMode(QFileDialog::ExistingFile);
            if (fd.exec() != QDialog::Accepted) return;
            QString path = fd.selectedFiles().first();
            if (!path.isEmpty()) {
                auto &mw = m_modelWidgets[key];
                if (path.startsWith(dir))
                    mw.mmprojFile->setText(path.mid(dir.length() + 1));
                else
                    mw.mmprojFile->setText(path);
            }
        });
        mmprojRow->addWidget(w.mmprojFile, 1);
        mmprojRow->addWidget(w.mmprojBrowseBtn);
        layout->addWidget(mmprojLabel, row, 0, Qt::AlignTop);
        layout->addLayout(mmprojRow, row, 1, 1, 3);
        row++;
    } else {
        w.mmprojFile = nullptr;
        w.mmprojBrowseBtn = nullptr;
    }

    // Parameters row
    auto *paramsLabel = new QLabel("Params:");
    paramsLabel->setStyleSheet("font-weight: bold; font-size: 11px;");
    layout->addWidget(paramsLabel, row, 0, Qt::AlignTop);

    auto *paramsWidget = new QWidget();
    auto *paramsLayout = new QHBoxLayout(paramsWidget);
    paramsLayout->setContentsMargins(0, 0, 0, 0);
    paramsLayout->setSpacing(6);

    // Port
    auto *portLayout = new QVBoxLayout();
    auto *portLabel = new QLabel("Port");
    portLabel->setStyleSheet("font-size: 9px; color: #8a8aaa;");
    w.port = new QSpinBox();
    w.port->setRange(1024, 65535);
    w.port->setFixedWidth(70);
    w.port->setToolTip("Port number for the model server.");
    portLayout->addWidget(portLabel);
    portLayout->addWidget(w.port);
    paramsLayout->addLayout(portLayout);

    // Context
    auto *ctxLayout = new QVBoxLayout();
    auto *ctxLabel = new QLabel("Context");
    ctxLabel->setStyleSheet("font-size: 9px; color: #8a8aaa;");
    w.ctx = new QSpinBox();
    w.ctx->setRange(0, 131072);
    w.ctx->setSingleStep(1024);
    w.ctx->setFixedWidth(80);
    w.ctx->setToolTip("Context size (number of tokens).");
    ctxLayout->addWidget(ctxLabel);
    ctxLayout->addWidget(w.ctx);
    paramsLayout->addLayout(ctxLayout);

    // NGL
    auto *nglLayout = new QVBoxLayout();
    auto *nglLabel = new QLabel("NGL");
    nglLabel->setStyleSheet("font-size: 9px; color: #8a8aaa;");
    w.ngl = new QSpinBox();
    w.ngl->setRange(0, 999);
    w.ngl->setFixedWidth(60);
    w.ngl->setToolTip("Number of GPU layers (-ngl).");
    nglLayout->addWidget(nglLabel);
    nglLayout->addWidget(w.ngl);
    paramsLayout->addLayout(nglLayout);

    // VRAM
    auto *vramLayout = new QVBoxLayout();
    auto *vramLabel = new QLabel("VRAM");
    vramLabel->setStyleSheet("font-size: 9px; color: #8a8aaa;");
    w.vram = new QSpinBox();
    w.vram->setRange(256, 65536);
    w.vram->setSuffix(" MB");
    w.vram->setSingleStep(512);
    w.vram->setFixedWidth(90);
    w.vram->setToolTip("Estimated VRAM usage in MB.");
    vramLayout->addWidget(vramLabel);
    vramLayout->addWidget(w.vram);
    paramsLayout->addLayout(vramLayout);

    // Batch size
    auto *batchLayout = new QVBoxLayout();
    auto *batchLabel = new QLabel("-b");
    batchLabel->setStyleSheet("font-size: 9px; color: #8a8aaa;");
    w.batchSize = new QSpinBox();
    w.batchSize->setRange(0, 65536);
    w.batchSize->setSingleStep(128);
    w.batchSize->setFixedWidth(70);
    w.batchSize->setToolTip("Batch size (-b).");
    batchLayout->addWidget(batchLabel);
    batchLayout->addWidget(w.batchSize);
    paramsLayout->addLayout(batchLayout);

    // UBatch size
    auto *ubatchLayout = new QVBoxLayout();
    auto *ubatchLabel = new QLabel("-ub");
    ubatchLabel->setStyleSheet("font-size: 9px; color: #8a8aaa;");
    w.ubatchSize = new QSpinBox();
    w.ubatchSize->setRange(0, 65536);
    w.ubatchSize->setSingleStep(128);
    w.ubatchSize->setFixedWidth(70);
    w.ubatchSize->setToolTip("Micro batch size (-ub).");
    ubatchLayout->addWidget(ubatchLabel);
    ubatchLayout->addWidget(w.ubatchSize);
    paramsLayout->addLayout(ubatchLayout);

    // Temperature
    auto *tempLayout = new QVBoxLayout();
    auto *tempLabel = new QLabel("Temp");
    tempLabel->setStyleSheet("font-size: 9px; color: #8a8aaa;");
    w.temperature = new QDoubleSpinBox();
    w.temperature->setRange(0.0, 2.0);
    w.temperature->setSingleStep(0.05);
    w.temperature->setDecimals(2);
    w.temperature->setFixedWidth(60);
    w.temperature->setToolTip("Temperature (--temp). Higher = more creative.");
    tempLayout->addWidget(tempLabel);
    tempLayout->addWidget(w.temperature);
    paramsLayout->addLayout(tempLayout);

    paramsLayout->addStretch();
    layout->addWidget(paramsWidget, row, 1, 1, 3);
    row++;

    m_modelWidgets[key] = w;
    return group;
}

// ===== ترکیبی از هر دو نسخه =====
void SettingsDialog::loadSettings() {
    // General
    m_themeCombo->setCurrentText(m_settings->theme());
    m_autoStartModels->setChecked(m_settings->autoStartModels());
    m_minimizeToTray->setChecked(m_settings->minimizeToTray());
    m_notifications->setChecked(m_settings->notificationsEnabled());

    // Paths
    m_llamaPath->setText(m_settings->llamaServerPath());
    m_modelsDir->setText(m_settings->modelsDirectory());
    m_vramLimit->setValue(m_settings->vramLimitMB());

    // Models
    for (const QString &key : m_settings->modelKeys()) {
        auto cfg = m_settings->modelConfig(key);
        auto it = m_modelWidgets.find(key);
        if (it == m_modelWidgets.end()) continue;

        auto &w = it.value();
        w.modelFile->setText(cfg.modelFile);
        if (w.mmprojFile) {
            w.mmprojFile->setText(cfg.mmprojFile);
        }
        w.port->setValue(cfg.port);
        w.ctx->setValue(cfg.ctx);
        w.ngl->setValue(cfg.ngl);
        w.vram->setValue(cfg.vramMB);
        w.batchSize->setValue(cfg.batchSize);
        w.ubatchSize->setValue(cfg.ubatchSize);
        w.temperature->setValue(cfg.temperature);
    }

    LOG_INFO("SettingsDialog", "Settings loaded");
}

void SettingsDialog::saveSettings() {
    // General
    m_settings->setTheme(m_themeCombo->currentText());
    m_settings->setAutoStartModels(m_autoStartModels->isChecked());
    m_settings->setMinimizeToTray(m_minimizeToTray->isChecked());
    m_settings->setNotificationsEnabled(m_notifications->isChecked());

    // Paths
    m_settings->setLlamaServerPath(m_llamaPath->text());
    m_settings->setModelsDirectory(m_modelsDir->text());
    m_settings->setVramLimitMB(m_vramLimit->value());

    // Models
    for (const QString &key : m_settings->modelKeys()) {
        auto it = m_modelWidgets.find(key);
        if (it == m_modelWidgets.end()) continue;

        auto &w = it.value();
        auto cfg = m_settings->modelConfig(key);

        cfg.modelFile = w.modelFile->text();
        if (w.mmprojFile) {
            cfg.mmprojFile = w.mmprojFile->text();
        }
        cfg.port = w.port->value();
        cfg.ctx = w.ctx->value();
        cfg.ngl = w.ngl->value();
        cfg.vramMB = w.vram->value();
        cfg.batchSize = w.batchSize->value();
        cfg.ubatchSize = w.ubatchSize->value();
        cfg.temperature = w.temperature->value();

        m_settings->setModelConfig(key, cfg);
    }

    m_settings->save();
    LOG_INFO("SettingsDialog", "Settings saved to " + m_settings->configFilePath());
}