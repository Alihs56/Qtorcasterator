#include "process_manager.h"
#include "logger.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaType>  // از نسخه جدید

const QString ProcessManager::SERVER_PATH = "/home/alireza/Work/AI/llama.cpp/build/bin/llama-server";

QStringList ProcessManager::buildArgs(const ModelConfig &config) {
    QStringList args;
    args << "-m" << config.modelPath;
    args << "-c" << QString::number(config.ctxSize);
    args << "--port" << QString::number(config.port);
    args << "-ngl" << QString::number(config.gpuLayers);
    args << "-b" << QString::number(config.batchSize);
    args << "-ub" << QString::number(config.ubatchSize);
    args << "--temp" << QString::number(config.temperature, 'f', 2);
    args << "--host" << "127.0.0.1";
    args << "--log-disable";

    if (config.embedding)
        args << "--embedding";

    if (config.vision && !config.mmprojPath.isEmpty()) {
        args << "--mmproj" << config.mmprojPath;
    }

    return args;
}

// ── ModelWorker ──────────────────────────────────────────

ModelWorker::ModelWorker(QObject *parent) : QObject(parent) {}

ModelWorker::~ModelWorker() {
    stopAll();
}

void ModelWorker::startModel(const ModelConfig &config) {
    if (m_processes.contains(config.port)) {
        emit modelError(config.port, "Already running");
        return;
    }

    if (!QFile::exists(ProcessManager::SERVER_PATH)) {
        emit modelError(config.port,
            "llama-server not found at " + ProcessManager::SERVER_PATH);
        return;
    }

    if (!QFile::exists(config.modelPath)) {
        emit modelError(config.port,
            "Model file not found: " + config.modelPath);
        return;
    }

    QProcess *proc = new QProcess(this);

    QStringList args = ProcessManager::buildArgs(config);

    connect(proc, &QProcess::readyReadStandardError, this, [this, proc, port = config.port]() {
        QString output = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        if (!output.isEmpty())
            emit modelOutput(port, output);
    });

    QFileInfo fi(config.modelPath);
    QString fileLabel = fi.fileName();
    if (fi.isSymLink()) {
        QString target = fi.symLinkTarget();
        if (!target.isEmpty())
            fileLabel = QFileInfo(target).fileName();
    }
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, port = config.port, fileLabel](int exitCode, QProcess::ExitStatus status) {
        QString reason = (status == QProcess::CrashExit) ? "crashed" : "exited";
        emit modelStopped(port, QString("%1 %2 (code %3)").arg(fileLabel, reason).arg(exitCode));
        QProcess *p = m_processes.take(port);
        if (p) p->deleteLater();
    });

    LOG_INFO("ProcessManager", "Starting " + fileLabel + "...");
    LOG_INFO("ProcessManager", "Args: " + args.join(" "));

    proc->start(ProcessManager::SERVER_PATH, args);

    if (proc->waitForStarted(10000)) {
        m_processes[config.port] = proc;
        LOG_INFO("ProcessManager", QString("%1 started").arg(fileLabel));
        emit modelStarted(config.port, fileLabel);
    } else {
        QString err = proc->errorString();
        LOG_ERROR("ProcessManager", QString("Failed to start %1: %2").arg(fileLabel).arg(err));
        emit modelError(config.port, "Failed to start: " + err);
        proc->deleteLater();
    }
}

void ModelWorker::stopModel(int port) {
    if (!m_processes.contains(port)) {
        QProcess killer;
        killer.start("pkill", {"-f", QString("llama-server.*%1").arg(port)});
        killer.waitForFinished(2000);
        return;
    }

    QProcess *proc = m_processes.take(port);
    if (!proc) return;

    proc->kill();
    if (!proc->waitForFinished(5000)) {
        proc->kill();
        proc->waitForFinished(3000);
    }
    emit modelStopped(port, "Stopped");
    proc->deleteLater();

    QProcess killer;
    killer.start("pkill", {"-f", QString("llama-server.*%1").arg(port)});
    killer.waitForFinished(2000);
}

void ModelWorker::stopAll() {
    QList<int> ports = m_processes.keys();
    for (int port : ports)
        stopModel(port);

    QProcess killer;
    killer.start("pkill", {"-f", "llama-server"});
    killer.waitForFinished(3000);
}

// ── ProcessManager ───────────────────────────────────────

ProcessManager::ProcessManager(QObject *parent) : QObject(parent) {
    // ===== از نسخه جدید: ثبت نوع با نام صریح =====
    qRegisterMetaType<ModelConfig>("ModelConfig");
    
    m_worker = new ModelWorker();
    m_worker->moveToThread(&m_workerThread);

    connect(&m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(m_worker, &ModelWorker::modelStarted, this, [this](int port, const QString &name) {
        m_runningCache[port] = true;
        emit modelStarted(port, name);
    });
    connect(m_worker, &ModelWorker::modelStopped, this, [this](int port, const QString &name) {
        m_runningCache[port] = false;
        emit modelStopped(port, name);
    });
    connect(m_worker, &ModelWorker::modelOutput, this, &ProcessManager::modelOutput);
    connect(m_worker, &ModelWorker::modelError, this, &ProcessManager::modelError);

    m_workerThread.start();
}

ProcessManager::~ProcessManager() {
    QMetaObject::invokeMethod(m_worker, "stopAll", Qt::QueuedConnection);
    m_workerThread.quit();
    m_workerThread.wait(10000);
}

void ProcessManager::launchModel(const ModelConfig &config) {
    QMetaObject::invokeMethod(m_worker, "startModel", Qt::QueuedConnection,
                              Q_ARG(ModelConfig, config));
}

void ProcessManager::terminateModel(int port) {
    m_runningCache[port] = false;
    QMetaObject::invokeMethod(m_worker, "stopModel", Qt::QueuedConnection,
                              Q_ARG(int, port));
}

void ProcessManager::terminateAll() {
    m_runningCache.clear();
    QMetaObject::invokeMethod(m_worker, "stopAll", Qt::QueuedConnection);
}

bool ProcessManager::isRunning(int port) const {
    return m_runningCache.value(port, false);
}

bool ProcessManager::isModelAlive(int port) const {
    return m_runningCache.value(port, false);
}