#ifndef VRAM_MANAGER_H
#define VRAM_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QPair>
#include <QSet>

class ProcessManager;

struct ModelInfo {
    QString name;
    QString serverPath;
    QString modelPath;
    QString mmprojPath;
    int port;
    int ngl;
    int ctx;
    bool isRunning = false;
    bool isVision = false;
    bool isEmbedding = false;
    int estimatedVRAM_MB;
    QString schedule; // "always" | "on-demand" | "rare"
    int batchSize = 2048;
    int ubatchSize = 512;
    double temperature = 0.7;
};

class VramManager : public QObject {
    Q_OBJECT
public:
    explicit VramManager(ProcessManager *pm, QObject *parent = nullptr);

    void registerModel(const QString &key, const ModelInfo &info);
    void setAlwaysOnModels(const QStringList &keys);

    bool ensureModel(const QString &key);
    bool ensureModelForScenario(const QString &scenario);

    void releaseModel(const QString &key);
    void releaseAll();

    int allocatedVRAM() const;
    int availableVRAM() const;
    int totalVRAM() const { return m_totalVRAM; }
    void setTotalVRAM(int mb);

    bool isModelLoaded(const QString &key) const;
    QStringList loadedModels() const;

    int scenarioToPort(const QString &scenario) const;
    QString scenarioToModelKey(const QString &scenario) const;

signals:
    void modelLoading(const QString &key);
    void modelLoaded(const QString &key);
    void modelUnloaded(const QString &key);
    void vramChanged(int allocated, int available);

private:
    bool startModel(const QString &key);
    bool stopModel(const QString &key);
    int findBestModelToUnload(const QString &exceptKey) const;

    ProcessManager *m_pm;
    QMap<QString, ModelInfo> m_models;
    QStringList m_alwaysOn;
    QSet<QString> m_pendingAllocations;
    int m_allocated = 0;
    int m_totalVRAM = 16384;
};

#endif