#ifndef LOGGER_H
#define LOGGER_H

#include <QObject>
#include <QString>
#include <QMutex>
#include <QDateTime>
#include <QFile>
#include <QTextStream>
#include <QQueue>
#include <QTimer>
#include <QDebug>

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger : public QObject {
    Q_OBJECT
public:
    static Logger* instance();

    void log(LogLevel level, const QString &source, const QString &message);
    void info(const QString &source, const QString &message);
    void warn(const QString &source, const QString &message);
    void error(const QString &source, const QString &message);
    void debug(const QString &source, const QString &message);

    QString levelToString(LogLevel level) const;
    QString formattedTimestamp() const;

    void setLogFile(const QString &path);
    void setMaxBuffer(int lines);

signals:
    void logEmitted(const QString &formatted, LogLevel level, const QString &source);

private:
    explicit Logger(QObject *parent = nullptr);
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void flushBuffer();

    QMutex m_mutex;
    QFile m_logFile;
    QTextStream m_fileStream;
    QQueue<QString> m_buffer;
    int m_maxBuffer = 1000;
    bool m_fileOpen = false;
};

#define LOG_INFO(src, msg)  Logger::instance()->info(src, msg)
#define LOG_WARN(src, msg)  Logger::instance()->warn(src, msg)
#define LOG_ERROR(src, msg) Logger::instance()->error(src, msg)
#define LOG_DEBUG(src, msg) Logger::instance()->debug(src, msg)

#endif