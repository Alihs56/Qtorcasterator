#include "monitor_widget.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>  // از نسخه قدیمی

// ── CircularGauge ─────────────────────────────────────────

CircularGauge::CircularGauge(const QString &title, QWidget *parent)
    : QWidget(parent), m_title(title) {
    setMinimumSize(100, 110);
    setMaximumSize(160, 140);
}

void CircularGauge::setValue(int percent) {
    m_percent = qBound(0, percent, 100);
    update();
}

void CircularGauge::setDetail(const QString &text) {
    m_detail = text;
    update();
}

void CircularGauge::setDark(bool dark) {
    m_dark = dark;
    update();
}

void CircularGauge::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int side = qMin(width(), height() - 20);
    int cx = width() / 2;
    int cy = (height() - 20) / 2 + 5;
    int r = side / 2 - 6;

    // Background arc
    QPen bgPen(m_dark ? QColor("#2a2a4a") : QColor("#d0d0d0"), 8);
    p.setPen(bgPen);
    p.drawArc(cx - r, cy - r, r * 2, r * 2, 0, 360 * 16);

    // Value arc
    QPen valPen(m_percent > 80 ? QColor("#e06c75") :
                    m_percent > 50 ? QColor("#e5c07b") : QColor("#98c379"), 8);
    p.setPen(valPen);
    int spanAngle = -m_percent * 360 / 100 * 16;
    p.drawArc(cx - r, cy - r, r * 2, r * 2, 90 * 16, spanAngle);

    // Percentage text
    p.setPen(m_dark ? QColor("#d0d0e4") : QColor("#333333"));
    QFont pf = font();
    pf.setPointSize(11);
    pf.setBold(true);
    p.setFont(pf);
    p.drawText(QRect(cx - r, cy - r - 4, r * 2, r * 2), Qt::AlignCenter,
               QString("%1%").arg(m_percent));

    // Title
    if (!m_title.isEmpty()) {
        QFont tf = font();
        tf.setPointSize(8);
        p.setFont(tf);
        p.setPen(m_dark ? QColor("#8a8aaa") : QColor("#666666"));
        p.drawText(QRect(0, height() - 18, width(), 18), Qt::AlignCenter, m_title);
    }
}

// ── MonitorWidget ─────────────────────────────────────────

MonitorWidget::MonitorWidget(QWidget *parent) : QWidget(parent) {
    setupUI();
}

void MonitorWidget::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(6);

    // CPU + GPU gauges row
    auto *gaugeRow = new QHBoxLayout();
    gaugeRow->setSpacing(4);

    m_cpuGauge = new CircularGauge("CPU");
    m_gpuGauge = new CircularGauge("GPU");

    gaugeRow->addWidget(m_cpuGauge);
    gaugeRow->addWidget(m_gpuGauge);
    mainLayout->addLayout(gaugeRow);

    // GPU status indicator
    m_gpuStatusLabel = new QLabel("\u25CF  GPU: OK");
    m_gpuStatusLabel->setStyleSheet("font-size: 11px; font-weight: bold; color: #98c379; padding: 2px 4px;");
    mainLayout->addWidget(m_gpuStatusLabel);

    // RAM
    auto *ramTitle = new QLabel("RAM");
    ramTitle->setStyleSheet("font-weight: bold; font-size: 11px; padding: 2px 0;");
    mainLayout->addWidget(ramTitle);

    m_ramLabel = new QLabel("--");
    m_ramLabel->setStyleSheet("font-size: 11px; font-family: Consolas; padding: 1px 4px; border-radius: 3px;");
    m_ramLabel->setWordWrap(true);
    mainLayout->addWidget(m_ramLabel);

    // VRAM
    auto *vramTitle = new QLabel("VRAM");
    vramTitle->setStyleSheet("font-weight: bold; font-size: 11px; padding: 2px 0;");
    mainLayout->addWidget(vramTitle);

    m_vramLabel = new QLabel("--");
    m_vramLabel->setStyleSheet("font-size: 11px; font-family: Consolas; padding: 1px 4px; border-radius: 3px;");
    m_vramLabel->setWordWrap(true);
    mainLayout->addWidget(m_vramLabel);

    // Temperatures
    auto *tempTitle = new QLabel("Temperatures");
    tempTitle->setStyleSheet("font-weight: bold; font-size: 11px; padding: 2px 0;");
    mainLayout->addWidget(tempTitle);

    QString tempStyle = "color: #e5c07b; font-size: 11px; font-family: Consolas; padding: 1px 4px; border-radius: 3px;";

    m_cpuTempLabel = new QLabel("CPU: --");
    m_cpuTempLabel->setStyleSheet(tempStyle);
    mainLayout->addWidget(m_cpuTempLabel);

    m_gpuTempLabel = new QLabel("GPU: --");
    m_gpuTempLabel->setStyleSheet(tempStyle);
    mainLayout->addWidget(m_gpuTempLabel);

    m_boardTempLabel = new QLabel("Board: --");
    m_boardTempLabel->setStyleSheet(tempStyle);
    mainLayout->addWidget(m_boardTempLabel);

    m_sysTempLabel = new QLabel("System: --");
    m_sysTempLabel->setStyleSheet(tempStyle);
    mainLayout->addWidget(m_sysTempLabel);

    // Fan Speeds
    auto *fanTitle = new QLabel("Fan Speeds");
    fanTitle->setStyleSheet("font-weight: bold; font-size: 11px; padding: 2px 0;");
    mainLayout->addWidget(fanTitle);

    QString fanStyle = "color: #61afef; font-size: 11px; font-family: Consolas; padding: 1px 4px; border-radius: 3px;";

    m_cpuFanLabel = new QLabel("CPU Fan: --");
    m_cpuFanLabel->setStyleSheet(fanStyle);
    mainLayout->addWidget(m_cpuFanLabel);

    m_gpuFanLabel = new QLabel("GPU Fan: --");
    m_gpuFanLabel->setStyleSheet(fanStyle);
    mainLayout->addWidget(m_gpuFanLabel);

    mainLayout->addStretch();

    // ===== از نسخه قدیمی: راه‌اندازی تایمر رفرش =====
    m_refreshTimer = new QTimer(this);
    m_refreshTimer->setInterval(2000);
    connect(m_refreshTimer, &QTimer::timeout, this, &MonitorWidget::refresh);
    m_refreshTimer->start();
}

void MonitorWidget::setDark(bool dark) {
    m_dark = dark;
    m_cpuGauge->setDark(dark);
    m_gpuGauge->setDark(dark);

    QString bg = dark ? "#1a1a2e" : "#f5f5f5";
    QString tc = dark ? "#d0d0e4" : "#333333";
    QString infoStyle = QString("font-size: 11px; font-family: Consolas; padding: 1px 4px; border-radius: 3px; color: %1; background-color: %2;").arg(tc, bg);

    m_ramLabel->setStyleSheet(infoStyle);
    m_vramLabel->setStyleSheet(infoStyle);

    QString tempColor = dark ? "#e5c07b" : "#cc8800";
    m_cpuTempLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-family: Consolas; padding: 1px 4px; border-radius: 3px;").arg(tempColor));
    m_gpuTempLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-family: Consolas; padding: 1px 4px; border-radius: 3px;").arg(tempColor));
    m_boardTempLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-family: Consolas; padding: 1px 4px; border-radius: 3px;").arg(tempColor));
    m_sysTempLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-family: Consolas; padding: 1px 4px; border-radius: 3px;").arg(tempColor));

    QString fanColor = dark ? "#61afef" : "#2266aa";
    m_cpuFanLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-family: Consolas; padding: 1px 4px; border-radius: 3px;").arg(fanColor));
    m_gpuFanLabel->setStyleSheet(QString("color: %1; font-size: 11px; font-family: Consolas; padding: 1px 4px; border-radius: 3px;").arg(fanColor));

    updateGpuStatusStyle();

    setStyleSheet(dark ? "background-color: transparent; color: #c0c0e0;" : "background-color: transparent; color: #333333;");
}

void MonitorWidget::updateGpuStatusStyle() {
    if (m_gpuError)
        m_gpuStatusLabel->setText("\u25CF  GPU: ERROR");
    else
        m_gpuStatusLabel->setText("\u25CF  GPU: OK");
    m_gpuStatusLabel->setStyleSheet(
        QString("font-size: 11px; font-weight: bold; color: %1; padding: 2px 4px;")
            .arg(m_gpuError
                     ? (m_dark ? "#e06c75" : "#cc3333")
                     : (m_dark ? "#98c379" : "#2e7d32")));
}

void MonitorWidget::refresh() {
    readCPULoad();
    readRAM();
    readSensors();
}

void MonitorWidget::refreshGPU() {
    readGPU();
}

void MonitorWidget::readCPULoad() {
    QFile f("/proc/stat");
    if (!f.open(QIODevice::ReadOnly)) return;

    QString line = QString::fromUtf8(f.readLine());
    f.close();

    QStringList parts = line.split(' ', Qt::SkipEmptyParts);
    if (parts.size() < 5) return;

    qulonglong user = parts[1].toULongLong();
    qulonglong nice = parts[2].toULongLong();
    qulonglong system = parts[3].toULongLong();
    qulonglong idle = parts[4].toULongLong();
    qulonglong iowait = parts.size() > 5 ? parts[5].toULongLong() : 0;
    qulonglong irq = parts.size() > 6 ? parts[6].toULongLong() : 0;
    qulonglong softirq = parts.size() > 7 ? parts[7].toULongLong() : 0;

    qulonglong total = user + nice + system + idle + iowait + irq + softirq;

    if (m_prevTotal > 0) {
        qulonglong diffIdle = idle - m_prevIdle;
        qulonglong diffTotal = total - m_prevTotal;
        int percent = 0;
        if (diffTotal > 0)
            percent = static_cast<int>(100.0 * (diffTotal - diffIdle) / diffTotal);
        m_cpuGauge->setValue(percent);
    }

    m_prevIdle = idle;
    m_prevTotal = total;
}

// ===== از نسخه قدیمی: readRAM با استفاده از /proc/meminfo =====
void MonitorWidget::readRAM() {
#ifdef __linux__
    QFile meminfo("/proc/meminfo");
    if (!meminfo.open(QIODevice::ReadOnly)) return;

    QString content = QString::fromUtf8(meminfo.readAll());
    meminfo.close();

    QStringList lines = content.split('\n');
    qulonglong memTotal = 0, memAvailable = 0;

    for (const QString &line : lines) {
        if (line.startsWith("MemTotal:")) {
            memTotal = line.section(' ', 1, 1).toULongLong() / 1024; // Convert to MB
        } else if (line.startsWith("MemAvailable:")) {
            memAvailable = line.section(' ', 1, 1).toULongLong() / 1024; // Convert to MB
        }
    }

    if (memTotal > 0 && memAvailable > 0) {
        qulonglong memUsed = memTotal - memAvailable;
        int percent = static_cast<int>(100.0 * memUsed / memTotal);
        m_ramLabel->setText(QString("Used: %1 MB / %2 MB (%3%)\nFree: %4 MB")
                                .arg(memUsed).arg(memTotal).arg(percent).arg(memAvailable));
    }
#else
    struct sysinfo si;
    if (sysinfo(&si) != 0) return;

    qulonglong memTotal = static_cast<qulonglong>(si.totalram) * si.mem_unit / (1024 * 1024);
    qulonglong memFree = static_cast<qulonglong>(si.freeram) * si.mem_unit / (1024 * 1024);
    qulonglong memUsed = memTotal - memFree;
    int percent = static_cast<int>(100.0 * memUsed / memTotal);

    m_ramLabel->setText(QString("Used: %1 MB / %2 MB (%3%)\nFree: %4 MB")
                            .arg(memUsed).arg(memTotal).arg(percent).arg(memFree));
#endif
}

void MonitorWidget::readGPU() {
    if (m_nvidiaBusy) return;

    // رفیق، به جای ساختن هر باره پروسس، چک کن اگه قبلاً ساخته شده استفاده کن
    if (!m_nvidiaProcess) {
        m_nvidiaProcess = new QProcess(this);
        connect(m_nvidiaProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this](int exitCode) {
            m_nvidiaBusy = false;
            if (exitCode == 0) {
                QString out = QString::fromUtf8(m_nvidiaProcess->readAllStandardOutput()).trimmed();
                parseNvidiaSmi(out);
            }
        });
    }

    m_nvidiaBusy = true;
    m_nvidiaProcess->start("nvidia-smi", {
        "--query-gpu=memory.used,memory.total,memory.free,utilization.gpu,temperature.gpu",
        "--format=csv,noheader,nounits"
    });
}

void MonitorWidget::readSensors() {
    if (m_sensorsBusy) return;
    if (m_sensorsProcess && m_sensorsProcess->state() != QProcess::NotRunning) return;
    m_sensorsBusy = true;

    auto *proc = new QProcess(this);
    if (m_sensorsProcess) m_sensorsProcess->deleteLater();
    m_sensorsProcess = proc;

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc](int exitCode, QProcess::ExitStatus) {
                m_sensorsBusy = false;
                if (proc != m_sensorsProcess) return;
                if (exitCode == 0) {
                    QString out = QString::fromUtf8(proc->readAllStandardOutput()).trimmed();
                    parseSensors(out);
                }
            });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError) {
        if (proc != m_sensorsProcess) return;
        m_sensorsBusy = false;
    });

    proc->start("sensors", {"-u"});
}

void MonitorWidget::parseNvidiaSmi(const QString &output) {
    QStringList parts = output.split(", ");
    if (parts.size() < 5) {
        m_gpuError = true;
        updateGpuStatusStyle();
        return;
    }

    bool hasError = false;
    for (const QString &p : parts) {
        QString trimmed = p.trimmed();
        if (trimmed == "N/A" || trimmed == "[N/A]" || trimmed.startsWith("ERR") || trimmed == "[Not Supported]") {
            hasError = true;
            break;
        }
    }

    bool ok;
    qulonglong total = parts[1].toULongLong(&ok);
    if (!ok || hasError) {
        m_gpuError = true;
        updateGpuStatusStyle();
        return;
    }

    m_gpuError = false;

    qulonglong used = parts[0].toULongLong(&ok);
    if (!ok) used = 0;
    qulonglong free = parts[2].toULongLong(&ok);
    if (!ok) free = 0;

    int vramPercent = total > 0 ? static_cast<int>(100.0 * used / total) : 0;
    m_vramLabel->setText(QString("Used: %1 MB / %2 MB (%3%)\nFree: %4 MB")
                             .arg(used).arg(total).arg(vramPercent).arg(free));

    int gpuUtil = parts[3].toInt(&ok);
    if (ok) m_gpuGauge->setValue(gpuUtil);

    int gpuTemp = parts[4].toInt(&ok);
    if (ok && gpuTemp > 0)
        m_gpuTempLabel->setText(QString("GPU: %1 \u00B0C").arg(gpuTemp));

    updateGpuStatusStyle();
}

// ===== از نسخه قدیمی: parseSensors کامل با QRegularExpression =====
void MonitorWidget::parseSensors(const QString &output) {
    QStringList lines = output.split('\n');

    QMap<QString, double> temps;
    QMap<QString, double> fans;

    QString currentAdapter;

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();

        // Detect adapter name
        if (trimmed.endsWith("-isa-0000") || trimmed.endsWith("-pci-0000") ||
            trimmed.contains("Adapter:")) {
            currentAdapter = trimmed;
            continue;
        }

        // Parse temp values with QRegularExpression (از نسخه قدیمی)
        QRegularExpression tempRe(R"(temp(\d+)_input:\s+([\d.]+))");
        QRegularExpressionMatch match = tempRe.match(trimmed);
        if (match.hasMatch()) {
            int index = match.captured(1).toInt();
            double value = match.captured(2).toDouble();
            temps[QString("temp%1").arg(index)] = value;
        }

        // Parse fan values with QRegularExpression (از نسخه قدیمی)
        QRegularExpression fanRe(R"(fan(\d+)_input:\s+([\d.]+))");
        QRegularExpressionMatch fanMatch = fanRe.match(trimmed);
        if (fanMatch.hasMatch()) {
            int index = fanMatch.captured(1).toInt();
            double value = fanMatch.captured(2).toDouble();
            fans[QString("fan%1").arg(index)] = value;
        }
    }

    // Assign temperatures based on available data
    QStringList tempKeys = temps.keys();
    if (tempKeys.size() >= 1) {
        m_cpuTempLabel->setText(QString("CPU: %1 \u00B0C").arg(temps[tempKeys[0]], 0, 'f', 0));
    }
    if (tempKeys.size() >= 2) {
        m_boardTempLabel->setText(QString("Board: %1 \u00B0C").arg(temps[tempKeys[1]], 0, 'f', 0));
    }
    if (tempKeys.size() >= 3) {
        m_sysTempLabel->setText(QString("System: %1 \u00B0C").arg(temps[tempKeys[2]], 0, 'f', 0));
    }

    // Assign fan speeds
    QStringList fanKeys = fans.keys();
    if (fanKeys.size() >= 1) {
        m_cpuFanLabel->setText(QString("CPU Fan: %1 RPM").arg(fans[fanKeys[0]], 0, 'f', 0));
    }
    if (fanKeys.size() >= 2) {
        m_gpuFanLabel->setText(QString("GPU Fan: %1 RPM").arg(fans[fanKeys[1]], 0, 'f', 0));
    }
}