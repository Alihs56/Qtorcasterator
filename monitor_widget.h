#ifndef MONITOR_WIDGET_H
#define MONITOR_WIDGET_H

#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTimer>
#include <QProcess>
#include <QPainter>
#include <QMap>
#include <sys/sysinfo.h>

class CircularGauge : public QWidget {
    Q_OBJECT
public:
    explicit CircularGauge(const QString &title, QWidget *parent = nullptr);
    void setValue(int percent);
    void setDetail(const QString &text);
    void setDark(bool dark);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_title;
    int m_percent = 0;
    QString m_detail;
    bool m_dark = true;
};

class MonitorWidget : public QWidget {
    Q_OBJECT
public:
    explicit MonitorWidget(QWidget *parent = nullptr);
    void setDark(bool dark);

public slots:
    void refresh();
    void refreshGPU();

private:
    void setupUI();
    void readCPULoad();
    void readRAM();
    void readSensors();
    void readGPU();
    void parseNvidiaSmi(const QString &output);
    void parseSensors(const QString &output);
    void updateGpuStatusStyle();

    CircularGauge *m_cpuGauge = nullptr;
    CircularGauge *m_gpuGauge = nullptr;
    QLabel *m_ramLabel = nullptr;
    QLabel *m_vramLabel = nullptr;
    QLabel *m_cpuTempLabel = nullptr;
    QLabel *m_gpuTempLabel = nullptr;
    QLabel *m_boardTempLabel = nullptr;
    QLabel *m_sysTempLabel = nullptr;
    QLabel *m_cpuFanLabel = nullptr;
    QLabel *m_gpuFanLabel = nullptr;
    QLabel *m_gpuStatusLabel = nullptr;

    QProcess *m_nvidiaProcess = nullptr;
    QProcess *m_sensorsProcess = nullptr;

    // CPU tracking
    qulonglong m_prevIdle = 0, m_prevTotal = 0;

    QTimer *m_refreshTimer = nullptr;  // از نسخه قدیمی

    bool m_nvidiaBusy = false;
    bool m_sensorsBusy = false;
    bool m_gpuError = false;
    bool m_dark = true;
};

#endif