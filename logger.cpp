#include "logger.h"
#include <QCoreApplication>
#include <QDir>

Logger* Logger::instance() {
    static Logger inst;
    return &inst;
}

Logger::Logger(QObject *parent) : QObject(parent), m_fileStream(&m_logFile) {
    QString logDir = QCoreApplication::applicationDirPath() + "/logs";
    QDir().mkpath(logDir);
    QString logPath = logDir + "/orchestrator.log";
    m_logFile.setFileName(logPath);
    if (m_logFile.open(QIODevice::Append | QIODevice::Text)) {
        m_fileOpen = true;
        m_fileStream << "\n=== Session Started: " << formattedTimestamp() << " ===\n";
        m_fileStream.flush();
    }
}

Logger::~Logger() {
    if (m_fileOpen) {
        m_fileStream << "=== Session Ended: " << formattedTimestamp() << " ===\n";
        m_logFile.close();
    }
}

// ===== از نسخه قدیمی: مدیریت کامل setLogFile با بازنشانی stream =====
void Logger::setLogFile(const QString &path) {
    QMutexLocker locker(&m_mutex);
    if (m_fileOpen) {
        m_fileStream.flush();
        m_logFile.close();
    }
    // Reset the stream to avoid stale references (از نسخه قدیمی)
    m_fileStream.setDevice(nullptr);
    m_logFile.setFileName(path);
    if (m_logFile.open(QIODevice::Append | QIODevice::Text)) {
        m_fileOpen = true;
        m_fileStream.setDevice(&m_logFile);
    } else {
        m_fileOpen = false;
    }
}

void Logger::setMaxBuffer(int lines) {
    QMutexLocker locker(&m_mutex);
    m_maxBuffer = lines;
    while (m_buffer.size() > m_maxBuffer)
        m_buffer.dequeue();
}

QString Logger::levelToString(LogLevel level) const {
    switch (level) {
    case LogLevel::Debug:   return "DEBUG";
    case LogLevel::Info:    return "INFO";
    case LogLevel::Warning: return "WARN";
    case LogLevel::Error:   return "ERROR";
    }
    return "UNKNOWN";
}

QString Logger::formattedTimestamp() const {
    return QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz");
}

// ===== ترکیب دو نسخه: لاگ با قفل و emit =====
void Logger::log(LogLevel level, const QString &source, const QString &message) {
    QMutexLocker locker(&m_mutex);

    QString ts = formattedTimestamp();
    QString lvl = levelToString(level);
    QString formatted = QString("[%1] [%2] [%3] %4").arg(ts, lvl, source, message);

    m_buffer.enqueue(formatted);
    if (m_buffer.size() > m_maxBuffer)
        m_buffer.dequeue();

    if (m_fileOpen) {
        m_fileStream << formatted << "\n";
        m_fileStream.flush();
    }

    // از نسخه جدید: unlock قبل از emit برای کاهش قفل
    locker.unlock();

    // Emit signals while lock is held (it's fine; signals are emitted synchronously)
    emit logEmitted(formatted, level, source);

    if (level == LogLevel::Error)
        qWarning().noquote() << formatted;
    else
        qDebug().noquote() << formatted;
}

void Logger::info(const QString &source, const QString &message) {
    log(LogLevel::Info, source, message);
}

void Logger::warn(const QString &source, const QString &message) {
    log(LogLevel::Warning, source, message);
}

void Logger::error(const QString &source, const QString &message) {
    log(LogLevel::Error, source, message);
}

void Logger::debug(const QString &source, const QString &message) {
    log(LogLevel::Debug, source, message);
}

void Logger::flushBuffer() {
    QMutexLocker locker(&m_mutex);
    m_buffer.clear();
}