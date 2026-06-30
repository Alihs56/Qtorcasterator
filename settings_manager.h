#ifndef SETTINGS_MANAGER_H
#define SETTINGS_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QByteArray>

struct ModelConfigEntry {
    QString key;
    QString name;
    QString modelFile;
    QString mmprojFile;
    int port;
    int ctx;
    int ngl;
    int vramMB;
    bool isEmbedding;
    bool isVision;
    int batchSize = 2048;
    int ubatchSize = 512;
    double temperature = 0.7;
};

class SettingsManager : public QObject {
    Q_OBJECT
public:
    explicit SettingsManager(QObject *parent = nullptr);

    QString configFilePath() const;

    // Load / Save
    void load();
    void save();
    void resetToDefaults();

    // General
    QString theme() const;
    void setTheme(const QString &v);
    bool autoStartModels() const;
    void setAutoStartModels(bool v);
    bool minimizeToTray() const;
    void setMinimizeToTray(bool v);
    bool notificationsEnabled() const;
    void setNotificationsEnabled(bool v);

    // Window
    QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray &v);
    QByteArray windowState() const;
    void setWindowState(const QByteArray &v);
    int mainSplitterPos() const;
    void setMainSplitterPos(int v);

    // Paths
    QString llamaServerPath() const;
    void setLlamaServerPath(const QString &v);
    QString modelsDirectory() const;
    void setModelsDirectory(const QString &v);
    int vramLimitMB() const;
    void setVramLimitMB(int v);

    // Per-model config
    QStringList modelKeys() const;
    ModelConfigEntry modelConfig(const QString &key) const;
    void setModelConfig(const QString &key, const ModelConfigEntry &cfg);
    int modelPort(const QString &key) const;
    void setModelPort(const QString &key, int port);

    // Session
    QString lastConversationFile() const;
    void setLastConversationFile(const QString &v);

signals:
    void settingsChanged();

private:
    QJsonObject m_root;
    QString m_filePath;

    void setValue(const QString &path, const QVariant &value);
    QVariant value(const QString &path, const QVariant &def = {}) const;

    static QJsonObject defaultConfig();
};

#endif