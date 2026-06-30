#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QImage>
#include <QList>
#include <QJsonObject>

/**
 * @brief Memory Manager — owns all runtime state for the AI agent.
 *
 * Responsibilities:
 * - Track current project, device, datasheet, editor context
 * - Maintain conversation summary
 * - Store current images and PDF
 * - Provide unified access to runtime memory for other modules
 */
class MemoryManager : public QObject
{
    Q_OBJECT
public:
    explicit MemoryManager(QObject *parent = nullptr);

    // ─── Current Context ───

    void setCurrentProject(const QString &path);
    QString currentProject() const;

    void setCurrentDevice(const QString &device);
    QString currentDevice() const;

    void setCurrentDatasheet(const QString &filepath);
    QString currentDatasheet() const;

    void setCurrentEditor(const QString &filePath, int line = 0, int column = 0);
    QString currentEditorFile() const;
    int currentEditorLine() const;
    int currentEditorColumn() const;

    void setCurrentSelection(const QString &text);
    QString currentSelection() const;

    void setCurrentLanguage(const QString &lang);
    QString currentLanguage() const;

    void setCurrentCompiler(const QString &compiler);
    QString currentCompiler() const;

    void setCurrentToolchain(const QString &toolchain);
    QString currentToolchain() const;

    // ─── Images & PDF ───

    void setCurrentImages(const QList<QImage> &images);
    QList<QImage> currentImages() const;

    void setCurrentPdf(const QString &filepath);
    QString currentPdf() const;

    // ─── Conversation ───

    void addTurn(const QString &role, const QString &text);
    QString conversationSummary() const;
    void clearConversation();

    // ─── Persistence ───

    QJsonObject toJson() const;
    void fromJson(const QJsonObject &json);
    void clear();

signals:
    void memoryChanged();

private:
    // ─── Context State ───
    QString m_currentProject;
    QString m_currentDevice;
    QString m_currentDatasheet;
    QString m_currentEditorFile;
    int m_currentEditorLine = 0;
    int m_currentEditorColumn = 0;
    QString m_currentSelection;
    QString m_currentLanguage;
    QString m_currentCompiler;
    QString m_currentToolchain;

    // ─── Media ───
    QList<QImage> m_currentImages;
    QString m_currentPdf;

    // ─── Conversation ───
    struct Turn { QString role; QString text; qint64 timestamp; };
    QList<Turn> m_conversation;
    QString m_summary;
};

#endif
