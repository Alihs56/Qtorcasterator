#include "settings_manager.h"
#include "logger.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QVariant>

SettingsManager::SettingsManager(QObject *parent) : QObject(parent) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_filePath = dir + "/config.json";
    load();
}

QString SettingsManager::configFilePath() const {
    return m_filePath;
}

static QStringList splitPath(const QString &path) {
    return path.split('/', Qt::SkipEmptyParts);
}

QVariant SettingsManager::value(const QString &path, const QVariant &def) const {
    QStringList parts = splitPath(path);
    QJsonObject obj = m_root;
    for (int i = 0; i < parts.size() - 1; ++i) {
        obj = obj[parts[i]].toObject();
    }
    QJsonValue v = obj[parts.last()];
    return v.isUndefined() ? def : v.toVariant();
}

static void setValueInMap(QVariantMap &map, const QStringList &parts, int idx, const QVariant &val) {
    if (idx == parts.size() - 1) {
        map[parts[idx]] = val;
        return;
    }
    QVariantMap child = map.value(parts[idx]).toMap();
    setValueInMap(child, parts, idx + 1, val);
    map[parts[idx]] = child;
}

void SettingsManager::setValue(const QString &path, const QVariant &val) {
    QStringList parts = splitPath(path);
    if (parts.isEmpty()) return;
    QVariantMap map = m_root.toVariantMap();
    setValueInMap(map, parts, 0, val);
    m_root = QJsonObject::fromVariantMap(map);
}

QJsonObject SettingsManager::defaultConfig() {
    QJsonObject cfg;

    QJsonObject general;
    general["theme"] = "dark";
    general["autoStartModels"] = false;
    general["minimizeToTray"] = false;
    general["notifications"] = true;
    cfg["general"] = general;

    QJsonObject paths;
    paths["llamaServer"] = "/home/alireza/Work/AI/llama.cpp/build/bin/llama-server";
    paths["modelsDir"] = "/home/alireza/Work/AI/Models/";
    paths["vramLimitMB"] = 16384;
    cfg["paths"] = paths;

    QJsonObject models;

    auto addModel = [&](const QString &key, const QString &name,
                        const QString &modelFile, const QString &mmproj,
                        int port, int ctx, int ngl, int vram,
                        bool embed = false, bool vision = false,
                        int batch = 2048, int ubatch = 512, double temp = 0.7) {
        QJsonObject m;
        m["name"] = name;
        m["modelFile"] = modelFile;
        m["mmprojFile"] = mmproj;
        m["port"] = port;
        m["ctx"] = ctx;
        m["ngl"] = ngl;
        m["vramMB"] = vram;
        m["embedding"] = embed;
        m["vision"] = vision;
        m["batchSize"] = batch;
        m["ubatchSize"] = ubatch;
        m["temperature"] = temp;
        models[key] = m;
    };

    addModel("planner", "Planner",  "gemma-4-E4B-it-Q4_K_M.gguf",                            "",      8001, 8192,  99, 4096);
    addModel("embed",   "Embed",    "nomic-embed-text-v1.5.Q4_K_M.gguf",                     "",      8005, 8192,  99, 9999, true, false, 8192, 1024);
    addModel("coder",   "Coder", "qwen2.5-coder-14b-instruct-q5_k_m-00001-of-00002.gguf", "",      8002, 16384, 35, 8192);
    addModel("expert",  "Expert","Qwen3-Coder-30B-A3B-Instruct-Q4_K_M.gguf",              "",      8003, 16384, 20, 6144);
    addModel("vision",  "Vision",   "gemma-4-E4B-it-Q4_K_M.gguf",   "mmproj-gemma-4-E4B-it-BF16.gguf", 8004, 8192,  99, 4096, false, true);

    cfg["models"] = models;

    cfg["session"] = QJsonObject();
    cfg["window"] = QJsonObject();

    return cfg;
}

void SettingsManager::load() {
    QFile f(m_filePath);
    if (f.exists() && f.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        f.close();
        if (doc.isObject()) {
            m_root = doc.object();
            LOG_INFO("Settings", "Loaded config from " + m_filePath);
            return;
        }
    }
    m_root = defaultConfig();
    LOG_INFO("Settings", "Using default config");
}

void SettingsManager::save() {
    QFile f(m_filePath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(m_root).toJson(QJsonDocument::Indented));
        f.close();
        LOG_INFO("Settings", "Saved config to " + m_filePath);
    }
    emit settingsChanged();
}

void SettingsManager::resetToDefaults() {
    m_root = defaultConfig();
    save();
}

// ── General ──

QString SettingsManager::theme() const { return value("general/theme", "dark").toString(); }
void SettingsManager::setTheme(const QString &v) { setValue("general/theme", v); save(); }

bool SettingsManager::autoStartModels() const { return value("general/autoStartModels", false).toBool(); }
void SettingsManager::setAutoStartModels(bool v) { setValue("general/autoStartModels", v); save(); }

bool SettingsManager::minimizeToTray() const { return value("general/minimizeToTray", false).toBool(); }
void SettingsManager::setMinimizeToTray(bool v) { setValue("general/minimizeToTray", v); save(); }

bool SettingsManager::notificationsEnabled() const { return value("general/notifications", true).toBool(); }
void SettingsManager::setNotificationsEnabled(bool v) { setValue("general/notifications", v); save(); }

// ── Window ──

QByteArray SettingsManager::windowGeometry() const {
    return QByteArray::fromBase64(value("window/geometry", "").toString().toUtf8());
}
void SettingsManager::setWindowGeometry(const QByteArray &v) {
    setValue("window/geometry", QString(v.toBase64())); save();
}

QByteArray SettingsManager::windowState() const {
    return QByteArray::fromBase64(value("window/state", "").toString().toUtf8());
}
void SettingsManager::setWindowState(const QByteArray &v) {
    setValue("window/state", QString(v.toBase64())); save();
}

int SettingsManager::mainSplitterPos() const { return value("window/mainSplitter", 220).toInt(); }
void SettingsManager::setMainSplitterPos(int v) { setValue("window/mainSplitter", v); save(); }

// ── Paths ──

QString SettingsManager::llamaServerPath() const {
    return value("paths/llamaServer", "/home/alireza/Work/AI/llama.cpp/build/bin/llama-server").toString();
}
void SettingsManager::setLlamaServerPath(const QString &v) { setValue("paths/llamaServer", v); save(); }

QString SettingsManager::modelsDirectory() const {
    return value("paths/modelsDir", "/home/alireza/Work/AI/Models/").toString();
}
void SettingsManager::setModelsDirectory(const QString &v) { setValue("paths/modelsDir", v); save(); }

int SettingsManager::vramLimitMB() const { return value("paths/vramLimitMB", 16384).toInt(); }
void SettingsManager::setVramLimitMB(int v) { setValue("paths/vramLimitMB", v); save(); }

// ── Models ──

QStringList SettingsManager::modelKeys() const {
    return m_root["models"].toObject().keys();
}

ModelConfigEntry SettingsManager::modelConfig(const QString &key) const {
    QJsonObject m = m_root["models"].toObject()[key].toObject();
    ModelConfigEntry e;
    e.key = key;
    e.name = m["name"].toString(key);
    e.modelFile = m["modelFile"].toString();
    e.mmprojFile = m["mmprojFile"].toString();
    e.port = m["port"].toInt(8001);
    e.ctx = m["ctx"].toInt(8192);
    e.ngl = m["ngl"].toInt(99);
    e.vramMB = m["vramMB"].toInt(4096);
    e.isEmbedding = m["embedding"].toBool(false);
    e.isVision = m["vision"].toBool(false);
    e.batchSize = m["batchSize"].toInt(2048);
    e.ubatchSize = m["ubatchSize"].toInt(512);
    e.temperature = m["temperature"].toDouble(0.7);
    return e;
}

void SettingsManager::setModelConfig(const QString &key, const ModelConfigEntry &cfg) {
    QJsonObject m;
    m["name"] = cfg.name;
    m["modelFile"] = cfg.modelFile;
    m["mmprojFile"] = cfg.mmprojFile;
    m["port"] = cfg.port;
    m["ctx"] = cfg.ctx;
    m["ngl"] = cfg.ngl;
    m["vramMB"] = cfg.vramMB;
    m["embedding"] = cfg.isEmbedding;
    m["vision"] = cfg.isVision;
    m["batchSize"] = cfg.batchSize;
    m["ubatchSize"] = cfg.ubatchSize;
    m["temperature"] = cfg.temperature;
    QJsonObject models = m_root["models"].toObject();
    models[key] = m;
    m_root["models"] = models;
    save();
}

int SettingsManager::modelPort(const QString &key) const {
    return modelConfig(key).port;
}
void SettingsManager::setModelPort(const QString &key, int port) {
    auto cfg = modelConfig(key);
    cfg.port = port;
    setModelConfig(key, cfg);
}

// ── Session ──

QString SettingsManager::lastConversationFile() const {
    return value("session/lastConversation", "").toString();
}
void SettingsManager::setLastConversationFile(const QString &v) {
    setValue("session/lastConversation", v); save();
}