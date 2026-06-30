#include "memory_manager.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>

MemoryManager::MemoryManager(QObject *parent)
    : QObject(parent)
{
}

// ─── Current Context ───

void MemoryManager::setCurrentProject(const QString &path)
{
    m_currentProject = path;
    emit memoryChanged();
}

QString MemoryManager::currentProject() const
{
    return m_currentProject;
}

void MemoryManager::setCurrentDevice(const QString &device)
{
    m_currentDevice = device;
    emit memoryChanged();
}

QString MemoryManager::currentDevice() const
{
    return m_currentDevice;
}

void MemoryManager::setCurrentDatasheet(const QString &filepath)
{
    m_currentDatasheet = filepath;
    emit memoryChanged();
}

QString MemoryManager::currentDatasheet() const
{
    return m_currentDatasheet;
}

void MemoryManager::setCurrentEditor(const QString &filePath, int line, int column)
{
    m_currentEditorFile = filePath;
    m_currentEditorLine = line;
    m_currentEditorColumn = column;
    emit memoryChanged();
}

QString MemoryManager::currentEditorFile() const
{
    return m_currentEditorFile;
}

int MemoryManager::currentEditorLine() const
{
    return m_currentEditorLine;
}

int MemoryManager::currentEditorColumn() const
{
    return m_currentEditorColumn;
}

void MemoryManager::setCurrentSelection(const QString &text)
{
    m_currentSelection = text;
    emit memoryChanged();
}

QString MemoryManager::currentSelection() const
{
    return m_currentSelection;
}

void MemoryManager::setCurrentLanguage(const QString &lang)
{
    m_currentLanguage = lang;
    emit memoryChanged();
}

QString MemoryManager::currentLanguage() const
{
    return m_currentLanguage;
}

void MemoryManager::setCurrentCompiler(const QString &compiler)
{
    m_currentCompiler = compiler;
    emit memoryChanged();
}

QString MemoryManager::currentCompiler() const
{
    return m_currentCompiler;
}

void MemoryManager::setCurrentToolchain(const QString &toolchain)
{
    m_currentToolchain = toolchain;
    emit memoryChanged();
}

QString MemoryManager::currentToolchain() const
{
    return m_currentToolchain;
}

// ─── Images & PDF ───

void MemoryManager::setCurrentImages(const QList<QImage> &images)
{
    m_currentImages = images;
    emit memoryChanged();
}

QList<QImage> MemoryManager::currentImages() const
{
    return m_currentImages;
}

void MemoryManager::setCurrentPdf(const QString &filepath)
{
    m_currentPdf = filepath;
    emit memoryChanged();
}

QString MemoryManager::currentPdf() const
{
    return m_currentPdf;
}

// ─── Conversation ───

void MemoryManager::addTurn(const QString &role, const QString &text)
{
    Turn t;
    t.role = role;
    t.text = text;
    t.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_conversation.append(t);

    if (m_conversation.size() > 200)
        m_conversation.removeFirst();

    emit memoryChanged();
}

QString MemoryManager::conversationSummary() const {
    if (m_conversation.isEmpty()) return "";
    QString summary;
    // فقط ۵ پیام آخر را با جزئیات کامل نگه دار، بقیه را خلاصه کن
    int startIdx = std::max(0, m_conversation.size() - 5);
    for (int i = startIdx; i < m_conversation.size(); ++i) {
        const Turn &t = m_conversation.at(i);
        summary += QString("[%1]: %2\n").arg(t.role, t.text);
    }
    return summary;
}

void MemoryManager::clearConversation()
{
    m_conversation.clear();
    emit memoryChanged();
}

// ─── Persistence ───

QJsonObject MemoryManager::toJson() const
{
    QJsonObject json;
    json["project"] = m_currentProject;
    json["device"] = m_currentDevice;
    json["datasheet"] = m_currentDatasheet;
    json["editorFile"] = m_currentEditorFile;
    json["editorLine"] = m_currentEditorLine;
    json["editorColumn"] = m_currentEditorColumn;
    json["selection"] = m_currentSelection;
    json["language"] = m_currentLanguage;
    json["compiler"] = m_currentCompiler;
    json["toolchain"] = m_currentToolchain;
    json["pdf"] = m_currentPdf;

    QJsonArray turns;
    for (const Turn &t : m_conversation)
        turns.append(QJsonObject{{"role", t.role}, {"text", t.text}, {"ts", (qint64)t.timestamp}});
    json["conversation"] = turns;

    return json;
}

void MemoryManager::fromJson(const QJsonObject &json)
{
    m_currentProject = json.value("project").toString();
    m_currentDevice = json.value("device").toString();
    m_currentDatasheet = json.value("datasheet").toString();
    m_currentEditorFile = json.value("editorFile").toString();
    m_currentEditorLine = json.value("editorLine").toInt();
    m_currentEditorColumn = json.value("editorColumn").toInt();
    m_currentSelection = json.value("selection").toString();
    m_currentLanguage = json.value("language").toString();
    m_currentCompiler = json.value("compiler").toString();
    m_currentToolchain = json.value("toolchain").toString();
    m_currentPdf = json.value("pdf").toString();

    m_conversation.clear();
    QJsonArray turns = json.value("conversation").toArray();
    for (const auto &v : turns) {
        QJsonObject o = v.toObject();
        Turn t;
        t.role = o.value("role").toString();
        t.text = o.value("text").toString();
        t.timestamp = static_cast<qint64>(o.value("ts").toDouble());
        m_conversation.append(t);
    }

    emit memoryChanged();
}

void MemoryManager::clear()
{
    m_currentProject.clear();
    m_currentDevice.clear();
    m_currentDatasheet.clear();
    m_currentEditorFile.clear();
    m_currentEditorLine = 0;
    m_currentEditorColumn = 0;
    m_currentSelection.clear();
    m_currentLanguage.clear();
    m_currentCompiler.clear();
    m_currentToolchain.clear();
    m_currentImages.clear();
    m_currentPdf.clear();
    m_conversation.clear();
    emit memoryChanged();
}
