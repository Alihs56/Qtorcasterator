#include "session_manager.h"
#include "logger.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QStandardPaths>

SessionManager::SessionManager(QObject *parent) : QObject(parent) {
    m_sessionDir = defaultSessionDir();
}

QString SessionManager::defaultSessionDir() {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dir + "/sessions";
}

QString SessionManager::timestampFilename() {
    return QDateTime::currentDateTime().toString("yyyy-MM-dd_HH-mm-ss") + ".json";
}

void SessionManager::setSessionDir(const QString &dir) {
    m_sessionDir = dir;
}

QString SessionManager::sessionDir() const {
    return m_sessionDir;
}

QJsonObject SessionManager::entryToJson(const ConversationEntry &entry) const {
    QJsonObject obj;
    obj["role"] = entry.role;
    obj["text"] = entry.text;
    obj["mode"] = entry.mode;
    obj["timestamp"] = entry.timestamp;
    return obj;
}

ConversationEntry SessionManager::entryFromJson(const QJsonObject &obj) const {
    ConversationEntry entry;
    entry.role = obj["role"].toString();
    entry.text = obj["text"].toString();
    entry.mode = obj["mode"].toString("chat");
    entry.timestamp = obj["timestamp"].toVariant().toLongLong();
    return entry;
}

bool SessionManager::saveConversation(const QString &filename,
                                      const QList<ConversationEntry> &entries) {
    QDir().mkpath(m_sessionDir);
    QString filepath = m_sessionDir + "/" + filename;

    QJsonArray arr;
    for (const auto &e : entries) {
        arr.append(entryToJson(e));
    }

    QJsonObject root;
    root["version"] = 1;
    root["app"] = "QtAIOrchestrator";
    root["savedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["entries"] = arr;

    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit sessionError("Cannot write: " + filepath);
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    file.close();

    LOG_INFO("Session", QString("Conversation saved: %1 (%2 entries)")
                 .arg(filename).arg(entries.size()));
    emit sessionSaved(filename);
    return true;
}

QList<ConversationEntry> SessionManager::loadConversation(const QString &filename) {
    QString filepath = m_sessionDir + "/" + filename;
    QList<ConversationEntry> entries;

    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit sessionError("Cannot read: " + filepath);
        return entries;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) {
        emit sessionError("Invalid session file: " + filename);
        return entries;
    }

    QJsonArray arr = doc.object()["entries"].toArray();
    for (const auto &val : arr) {
        entries.append(entryFromJson(val.toObject()));
    }

    LOG_INFO("Session", QString("Conversation loaded: %1 (%2 entries)")
                 .arg(filename).arg(entries.size()));
    emit sessionLoaded(filename);
    return entries;
}

bool SessionManager::saveSessionState(const QString &filename, const SessionState &state) {
    QDir().mkpath(m_sessionDir);
    QString filepath = m_sessionDir + "/" + filename;

    QJsonObject root;
    root["version"] = 1;
    root["app"] = "QtAIOrchestrator";
    root["savedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["currentMode"] = state.currentMode;
    root["lastQuery"] = state.lastQuery;

    // ===== از نسخه جدید: ذخیره conversation کامل =====
    QJsonArray convArr;
    for (const auto &e : state.conversation) {
        convArr.append(entryToJson(e));
    }
    root["conversation"] = convArr;

    QJsonArray portsArr;
    for (const auto &p : state.activeModelPorts) portsArr.append(p);
    root["activeModelPorts"] = portsArr;

    QFile file(filepath);
    if (!file.open(QIODevice::WriteOnly)) {
        emit sessionError("Cannot write state: " + filepath);
        return false;
    }
    file.write(QJsonDocument(root).toJson());
    file.close();

    LOG_INFO("Session", "Session state saved: " + filename);
    return true;
}

SessionState SessionManager::loadSessionState(const QString &filename) {
    QString filepath = m_sessionDir + "/" + filename;
    SessionState state;

    QFile file(filepath);
    if (!file.open(QIODevice::ReadOnly)) return state;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    if (!doc.isObject()) return state;

    QJsonObject root = doc.object();
    state.currentMode = root["currentMode"].toString("chat");
    state.lastQuery = root["lastQuery"].toString();

    // ===== از نسخه جدید: بارگذاری conversation کامل =====
    QJsonArray convArr = root["conversation"].toArray();
    for (const auto &val : convArr) {
        state.conversation.append(entryFromJson(val.toObject()));
    }

    QJsonArray portsArr = root["activeModelPorts"].toArray();
    for (const auto &val : portsArr)
        state.activeModelPorts << val.toString();

    return state;
}

QStringList SessionManager::listSessions() const {
    QDir dir(m_sessionDir);
    if (!dir.exists()) return {};
    QStringList filters;
    filters << "*.json";
    QStringList files = dir.entryList(filters, QDir::Files, QDir::Time);
    for (auto &f : files) {
        f = m_sessionDir + "/" + f;
    }
    return files;
}

bool SessionManager::deleteSession(const QString &filename) {
    QString filepath = m_sessionDir + "/" + filename;
    if (QFile::remove(filepath)) {
        LOG_INFO("Session", "Deleted: " + filename);
        return true;
    }
    return false;
}