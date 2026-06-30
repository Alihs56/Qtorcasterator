#include "tool_manager.h"
#include "logger.h"
#include <QProcess>
#include <QFileInfo>

ToolManager::ToolManager(QObject *parent)
    : QObject(parent)
{
}

// ─── PDF ───

void ToolManager::extractPdfText(const QString &filepath, std::function<void(const QString&)> callback)
{
    QProcess proc;
    proc.start("pdftotext", {"-layout", filepath, "-"});
    if (proc.waitForFinished(30000)) {
        QString text = QString::fromUtf8(proc.readAllStandardOutput());
        if (callback) callback(text);
    } else {
        LOG_ERROR("ToolManager", "pdftotext failed for: " + filepath);
        if (callback) callback({});
    }
}

// ─── Image ───

QImage ToolManager::loadImage(const QString &filepath)
{
    QImage img(filepath);
    return img;
}

bool ToolManager::validateImage(const QImage &image)
{
    return !image.isNull() && image.width() > 0 && image.height() > 0;
}

// ─── Terminal ───

int ToolManager::executeCommand(const QString &command, const QString &workingDir)
{
    int id = m_nextCommandId++;

    QProcess *proc = new QProcess(this);
    if (!workingDir.isEmpty())
        proc->setWorkingDirectory(workingDir);

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, id, proc]() {
        emit commandOutput(id, QString::fromUtf8(proc->readAllStandardOutput()));
    });

    connect(proc, &QProcess::readyReadStandardError, this, [this, id, proc]() {
        emit commandError(id, QString::fromUtf8(proc->readAllStandardError()));
    });

    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, id, proc](int exitCode, QProcess::ExitStatus) {
        emit commandFinished(id, exitCode);
        proc->deleteLater();
    });

    proc->start(command);
    if (!proc->waitForStarted(5000)) {
        LOG_ERROR("ToolManager", "Failed to start command: " + command);
        emit commandError(id, "Failed to start: " + command);
        emit commandFinished(id, -1);
        proc->deleteLater();
        return -1;
    }

    return id;
}

void ToolManager::cancelCommand(int id)
{
    QList<QProcess*> children = findChildren<QProcess*>();
    for (QProcess *proc : children) {
        if (proc->property("commandId").toInt() == id) {
            proc->kill();
            proc->waitForFinished(3000);
            break;
        }
    }
}
