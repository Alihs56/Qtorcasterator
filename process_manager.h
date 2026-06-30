#ifndef PROCESS_MANAGER_H
#define PROCESS_MANAGER_H

#include <QObject>
#include <QProcess>
#include <QMap>
#include <QThread>
#include <QStringList>
#include <QDir>
#include <QFile>
#include <QMetaType>

struct ModelConfig {
    QString name;
    QString modelPath;
    QString mmprojPath;
    int port;
    int ctxSize;
    int gpuLayers;
    bool embedding;
    bool alwaysOn;
    bool vision;
    int batchSize = 2048;
    int ubatchSize = 512;
    double temperature = 0.7;
};

Q_DECLARE_METATYPE(ModelConfig)

class ModelWorker : public QObject {
    Q_OBJECT
public:
    explicit ModelWorker(QObject *parent = nullptr);
    ~ModelWorker();

public slots:
    void startModel(const ModelConfig &config);
    void stopModel(int port);
    void stopAll();

signals:
    void modelStarted(int port, const QString &name);
    void modelStopped(int port, const QString &name);
    void modelOutput(int port, const QString &output);
    void modelError(int port, const QString &error);

private:
    QMap<int, QProcess*> m_processes;
};

class ProcessManager : public QObject {
    Q_OBJECT
public:
    explicit ProcessManager(QObject *parent = nullptr);
    ~ProcessManager();

    void launchModel(const ModelConfig &config);
    void terminateModel(int port);
    void terminateAll();
    bool isRunning(int port) const;
    bool isModelAlive(int port) const;

    static const QString SERVER_PATH;
    static QStringList buildArgs(const ModelConfig &config);

signals:
    void modelStarted(int port, const QString &name);
    void modelStopped(int port, const QString &name);
    void modelOutput(int port, const QString &output);
    void modelError(int port, const QString &error);
    void allStopped();

private:
    QThread m_workerThread;
    ModelWorker *m_worker;
    mutable QMap<int, bool> m_runningCache;
};

#endif