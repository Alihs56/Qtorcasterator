#include "terminal_executor.h"
#include "logger.h"
#include <QDir>
#include <QTimer>
#include <QPointer>

TerminalExecutor::TerminalExecutor(QObject *parent) : QObject(parent) {}

void TerminalExecutor::setWorkingDir(const QString &dir) {
    m_defaultWorkDir = dir;
}

QString TerminalExecutor::workingDir() const {
    return m_defaultWorkDir;
}

// ===== از نسخه قدیمی: execute کامل با timeoutFlags و QPointer =====
int TerminalExecutor::execute(const QString &command, const QStringList &args,
                              const QString &workDir, int timeoutMs) {
    int id = m_nextId++;

    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::MergedChannels);

    QString wd = workDir.isEmpty() ? m_defaultWorkDir : workDir;
    if (!wd.isEmpty()) {
        QDir dir(wd);
        if (!dir.exists()) {
            LOG_WARN("Terminal", QString("Working directory does not exist: %1").arg(wd));
        }
        proc->setWorkingDirectory(wd);
    }

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc, id]() {
        QByteArray data = proc->readAllStandardOutput();
        QString text = QString::fromUtf8(data);
        m_outputBuffer[id] += text;
        emit outputReady(id, text);
    });

    connect(proc, &QProcess::readyReadStandardError, this, [this, proc, id]() {
        QByteArray data = proc->readAllStandardError();
        QString text = QString::fromUtf8(data);
        m_errorBuffer[id] += text;
        emit errorOutput(id, text);
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, proc, id, command](int exitCode, QProcess::ExitStatus status) {
                Q_UNUSED(status)

                // ===== از نسخه قدیمی: بررسی timeout =====
                bool timedOut = m_timeoutFlags.value(id, false);
                m_timeoutFlags.remove(id);

                QString fullOutput = m_outputBuffer.take(id);
                m_errorBuffer.remove(id);
                m_processes.remove(id);

                if (timedOut) {
                    LOG_WARN("Terminal", QString("[%1] Process timed out and was killed").arg(id));
                    emit finished(id, -1, fullOutput + "\n[ERROR: Process timed out]");
                } else {
                    LOG_INFO("Terminal", QString("[%1] exit=%2 | %3").arg(id).arg(exitCode).arg(command.left(80)));
                    emit finished(id, exitCode, fullOutput);
                }

                proc->deleteLater();
                if (!hasRunningJobs()) emit allFinished();
            });

    m_processes[id] = proc;
    m_outputBuffer[id] = QString();
    m_errorBuffer[id] = QString();
    m_timeoutFlags[id] = false;

    QString cmdLine = command + " " + args.join(" ");
    emit started(id, cmdLine);

    LOG_INFO("Terminal", QString("[%1] $ %2").arg(id).arg(cmdLine));
    proc->start(command, args);

    // ===== از نسخه قدیمی: timeout با QPointer =====
    if (timeoutMs > 0) {
        QPointer<QProcess> safeProc(proc);
        int currentId = id;

        QTimer::singleShot(timeoutMs, this, [this, currentId, safeProc, timeoutMs]() {
            if (!m_processes.contains(currentId)) {
                // Process already finished or was cancelled
                return;
            }

            QProcess *p = m_processes.value(currentId);
            if (p && p->state() != QProcess::NotRunning) {
                LOG_WARN("Terminal", QString("[%1] Timeout after %2ms - killing process")
                             .arg(currentId).arg(timeoutMs));
                m_timeoutFlags[currentId] = true;
                p->kill();
                if (!p->waitForFinished(3000)) {
                    p->terminate();
                    p->waitForFinished(1000);
                }
            } else {
                // Process finished but signals might not have been processed yet
                LOG_INFO("Terminal", QString("[%1] Timeout check: process already finished")
                                         .arg(currentId));
                m_timeoutFlags.remove(currentId);
            }
        });
    }

    return id;
}

// بازنویسی با فیلتر امنیتی و مدیریت خطا
int TerminalExecutor::executeShell(const QString &shellCommand, const QString &workDir, int timeoutMs) {
    // ۱. بررسی امنیتی: جلوگیری از اجرای دستورات چندگانه مخرب
    static const QRegularExpression unsafeChars(R"([;&|`$()])");
    if (shellCommand.contains(unsafeChars)) {
        LOG_ERROR("Terminal", "Blocked unsafe shell command execution");
        return -1;
    }

    // ۲. محدود کردن طول دستور برای جلوگیری از Buffer Overflow در سطح سیستم
    if (shellCommand.length() > 1024) {
        LOG_ERROR("Terminal", "Command too long");
        return -1;
    }

    LOG_INFO("Terminal", "Executing secure shell command: " + shellCommand);
    
    // استفاده از bash -c برای اجرای دستور در محیط استاندارد لینوکس
    return execute("/bin/bash", {"-c", shellCommand}, workDir, timeoutMs);
}

// ===== از نسخه قدیمی: cancel با مدیریت timeoutFlags =====
void TerminalExecutor::cancel(int id) {
    if (!m_processes.contains(id)) {
        LOG_WARN("Terminal", QString("Attempted to cancel non-existent process %1").arg(id));
        return;
    }

    QProcess *proc = m_processes.take(id);
    m_timeoutFlags.remove(id);

    if (proc->state() != QProcess::NotRunning) {
        LOG_INFO("Terminal", QString("[%1] Cancelling process...").arg(id));
        proc->kill();
        proc->waitForFinished(3000);
    }

    QString output = m_outputBuffer.take(id);
    m_errorBuffer.remove(id);

    proc->deleteLater();
    emit finished(id, -1, output + "\n[INFO: Process cancelled]");

    if (!hasRunningJobs()) emit allFinished();
}

void TerminalExecutor::cancelAll() {
    QList<int> ids = m_processes.keys();
    LOG_INFO("Terminal", QString("Cancelling %1 running processes").arg(ids.size()));
    for (int id : ids) {
        cancel(id);
    }
}

bool TerminalExecutor::isRunning(int id) const {
    return m_processes.contains(id);
}

bool TerminalExecutor::hasRunningJobs() const {
    return !m_processes.isEmpty();
}