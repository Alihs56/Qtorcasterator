#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>

struct ConversationEntry {
    QString role;
    QString text;
    QString mode;
    qint64 timestamp;
};

struct SessionState {
    QList<ConversationEntry> conversation;  // از نسخه جدید
    QString currentMode;
    QString lastQuery;
    QStringList activeModelPorts;
};

class SessionManager : public QObject {
    Q_OBJECT
public:
    explicit SessionManager(QObject *parent = nullptr);

    void setSessionDir(const QString &dir);
    QString sessionDir() const;

    bool saveConversation(const QString &filename,
                          const QList<ConversationEntry> &entries);
    QList<ConversationEntry> loadConversation(const QString &filename);

    bool saveSessionState(const QString &filename, const SessionState &state);
    SessionState loadSessionState(const QString &filename);

    QStringList listSessions() const;
    bool deleteSession(const QString &filename);

    static QString defaultSessionDir();
    static QString timestampFilename();

signals:
    void sessionSaved(const QString &filename);
    void sessionLoaded(const QString &filename);
    void sessionError(const QString &error);

private:
    QJsonObject entryToJson(const ConversationEntry &entry) const;
    ConversationEntry entryFromJson(const QJsonObject &obj) const;

    QString m_sessionDir;
};

#endif