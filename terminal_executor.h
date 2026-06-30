#ifndef TERMINAL_EXECUTOR_H
#define TERMINAL_EXECUTOR_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QByteArray>
#include <QPointer>
#include <QRegularExpression>

class TerminalExecutor : public QObject {
    Q_OBJECT
public:
    explicit TerminalExecutor(QObject *parent = nullptr);

    int execute(const QString &command, const QStringList &args = {},
                const QString &workDir = {}, int timeoutMs = 60000);
    int executeShell(const QString &shellCommand, const QString &workDir = {},
                     int timeoutMs = 120000);

    void cancel(int id);
    void cancelAll();

    bool isRunning(int id) const;
    bool hasRunningJobs() const;

    void setWorkingDir(const QString &dir);
    QString workingDir() const;

signals:
    void started(int id, const QString &command);
    void outputReady(int id, const QString &output);
    void errorOutput(int id, const QString &output);
    void finished(int id, int exitCode, const QString &fullOutput);
    void allFinished();

private:
    int m_nextId = 1;
    QString m_defaultWorkDir;
    QMap<int, QProcess*> m_processes;
    QMap<int, QString> m_outputBuffer;
    QMap<int, QString> m_errorBuffer;
    QMap<int, bool> m_timeoutFlags;  // Track if a process timed out (از نسخه قدیمی)
};

#endif