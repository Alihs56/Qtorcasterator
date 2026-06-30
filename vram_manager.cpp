#include "vram_manager.h"
#include "process_manager.h"
#include "logger.h"
#include <algorithm>
#include <functional>

VramManager::VramManager(ProcessManager *pm, QObject *parent)
    : QObject(parent), m_pm(pm), m_totalVRAM(16384) {

    // Track model lifecycle so VRAM bookkeeping stays correct
    // even when processes crash or are killed externally
    connect(m_pm, &ProcessManager::modelStarted, this, [this](int port, const QString &name) {
        Q_UNUSED(name)
        for (auto it = m_models.begin(); it != m_models.end(); ++it) {
            if (it.value().port == port) {
                it.value().isRunning = true;
                m_pendingAllocations.remove(it.key());
                LOG_INFO("VRAM", QString("Model '%1' confirmed running on port %2.")
                             .arg(it.key()).arg(port));
                emit modelLoaded(it.key());
            }
        }
    });

    connect(m_pm, &ProcessManager::modelStopped, this, [this](int port, const QString &name) {
        for (auto it = m_models.begin(); it != m_models.end(); ++it) {
            if (it.value().port == port) {
                if (it.value().isRunning) {
                    it.value().isRunning = false;
                    m_allocated -= it.value().estimatedVRAM_MB;
                    emit modelUnloaded(it.key());
                    emit vramChanged(m_allocated, availableVRAM());
                    LOG_INFO("VRAM", QString("Model '%1' stopped (port %2). VRAM: %3/%4 MB")
                                 .arg(it.key()).arg(port).arg(m_allocated).arg(totalVRAM()));
                } else if (m_pendingAllocations.remove(it.key())) {
                    it.value().isRunning = false;
                    m_allocated -= it.value().estimatedVRAM_MB;
                    emit modelUnloaded(it.key());
                    emit vramChanged(m_allocated, availableVRAM());
                    LOG_INFO("VRAM", QString("Model '%1' crashed before ready (port %2). Cleaned up %3 MB. VRAM: %4/%5 MB")
                                 .arg(it.key()).arg(port).arg(it.value().estimatedVRAM_MB).arg(m_allocated).arg(totalVRAM()));
                } else {
                    LOG_INFO("VRAM", QString("Model '%1' stopped (port %2) — was not tracked as running.")
                                 .arg(it.key()).arg(port));
                }
            }
        }
    });
}

void VramManager::setTotalVRAM(int mb) {
    m_totalVRAM = mb;
}

void VramManager::registerModel(const QString &key, const ModelInfo &info) {
    m_models[key] = info;
    LOG_INFO("VRAM", QString("Registered model '%1' (port=%2, VRAM=%3MB)")
                 .arg(key).arg(info.port).arg(info.estimatedVRAM_MB));
}

void VramManager::setAlwaysOnModels(const QStringList &keys) {
    m_alwaysOn = keys;
    for (const QString &k : keys) {
        if (m_models.contains(k)) {
            ensureModel(k);
        }
    }
}

bool VramManager::ensureModel(const QString &key) {
    if (!m_models.contains(key)) {
        LOG_ERROR("VRAM", "Unknown model: " + key);
        return false;
    }

    if (isModelLoaded(key)) return true;

    ModelInfo &info = m_models[key];

    // Free VRAM until the target model fits
    while (m_allocated + info.estimatedVRAM_MB > totalVRAM()) {
        int victim = findBestModelToUnload(key);
        if (victim < 0) {
            LOG_ERROR("VRAM", QString("Cannot free enough VRAM for '%1' (need %2MB, have %3MB free)")
                         .arg(key).arg(info.estimatedVRAM_MB).arg(availableVRAM()));
            return false;
        }
        stopModel(m_models.keys().at(victim));
    }

    return startModel(key);
}

void VramManager::releaseModel(const QString &key) {
    if (m_alwaysOn.contains(key)) {
        LOG_WARN("VRAM", "Cannot release always-on model: " + key);
        return;
    }
    stopModel(key);
}

void VramManager::releaseAll() {
    QStringList keys = m_models.keys();
    for (const QString &k : keys) {
        if (m_alwaysOn.contains(k)) continue;
        stopModel(k);
    }
}

int VramManager::allocatedVRAM() const {
    return m_allocated;
}

int VramManager::availableVRAM() const {
    return totalVRAM() - m_allocated;
}

bool VramManager::isModelLoaded(const QString &key) const {
    return m_models.contains(key) && m_models[key].isRunning;
}

QStringList VramManager::loadedModels() const {
    QStringList result;
    for (auto it = m_models.begin(); it != m_models.end(); ++it) {
        if (it.value().isRunning) result << it.key();
    }
    return result;
}

int VramManager::scenarioToPort(const QString &scenario) const {
    QString key = scenarioToModelKey(scenario);
    if (m_models.contains(key)) return m_models[key].port;
    return -1;
}

QString VramManager::scenarioToModelKey(const QString &scenario) const {
    if (scenario == "code" || scenario == "build") return "coder";
    if (scenario == "vision") return "vision";
    if (scenario == "plan") return "planner";
    if (scenario == "deep") return "expert";
    return "planner";
}

bool VramManager::ensureModelForScenario(const QString &scenario) {
    return ensureModel(scenarioToModelKey(scenario));
}

bool VramManager::startModel(const QString &key) {
    ModelInfo &info = m_models[key];
    if (info.isRunning) return true;

    emit modelLoading(key);
    LOG_INFO("VRAM", QString("Starting model '%1'... ctx=%2 batch=%3 ubatch=%4")
             .arg(key).arg(info.ctx).arg(info.batchSize).arg(info.ubatchSize));

    ModelConfig cfg;
    cfg.name = info.name;
    cfg.modelPath = info.modelPath;
    cfg.mmprojPath = info.mmprojPath;
    cfg.port = info.port;
    cfg.ctxSize = info.ctx;
    cfg.gpuLayers = info.ngl;
    cfg.embedding = (key == "embed");
    cfg.alwaysOn = m_alwaysOn.contains(key);
    cfg.vision = info.isVision;
    cfg.batchSize = info.batchSize;
    cfg.ubatchSize = info.ubatchSize;
    cfg.temperature = info.temperature;

    m_pm->launchModel(cfg);
    m_pendingAllocations.insert(key);
    m_allocated += info.estimatedVRAM_MB;
    // ProcessManager will emit modelStarted signal; the constructor
    // connection will set isRunning=true at that point.
    emit vramChanged(m_allocated, availableVRAM());
    LOG_INFO("VRAM", QString("Model '%1' allocated. VRAM: %2/%3 MB")
                 .arg(key).arg(m_allocated).arg(totalVRAM()));
    return true;
}

// بازنویسی با مکانیزم تایید توقف (Confirmation)
bool VramManager::stopModel(const QString &key) {
    if (!m_models.contains(key)) return false;
    
    ModelInfo &info = m_models[key];
    if (!info.isRunning) return true;

    LOG_INFO("VRAM", "Attempting to terminate model: " + key);
    
    // ۱. ارسال دستور توقف به مدیر پروسس
    m_pm->terminateModel(info.port);
    
    // ۲. انتظار برای تایید واقعی توقف (با تایم اوت)
    bool success = false;
    for(int i=0; i<10; ++i) { // حداکثر ۲ ثانیه صبر
        if (!m_pm->isRunning(info.port)) {
            success = true;
            break;
        }
        QThread::msleep(200);
    }

    // ۳. فقط در صورت اطمینان از بسته شدن، محاسبات VRAM را بروزرسانی کن
    if (success) {
        info.isRunning = false;
        m_allocated -= info.estimatedVRAM_MB;
        if (m_allocated < 0) m_allocated = 0;
        
        emit modelUnloaded(key);
        emit vramChanged(m_allocated, availableVRAM());
        LOG_INFO("VRAM", "Model " + key + " stopped and VRAM cleared.");
    } else {
        LOG_ERROR("VRAM", "Failed to confirm model termination: " + key);
    }

    return success;
}

int VramManager::findBestModelToUnload(const QString &exceptKey) const {
    int bestIdx = -1;
    int largestVRAM = 0;
    auto keys = m_models.keys();
    for (int i = 0; i < keys.size(); ++i) {
        const QString &k = keys[i];
        if (k == exceptKey) continue;
        if (m_alwaysOn.contains(k)) continue;
        const ModelInfo &info = m_models[k];
        if (info.isRunning && info.estimatedVRAM_MB > largestVRAM) {
            largestVRAM = info.estimatedVRAM_MB;
            bestIdx = i;
        }
    }
    return bestIdx;
}