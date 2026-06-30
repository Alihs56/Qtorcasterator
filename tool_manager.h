#ifndef TOOL_MANAGER_H
#define TOOL_MANAGER_H

#include <QObject>
#include <QString>
#include <QImage>
#include <QList>
#include <functional>

/**
 * @brief Tool Manager — executes external tools.
 *
 * Responsibilities:
 * - PDF text extraction
 * - Image loading and validation
 * - Terminal command execution
 * - Future tools can be added without modifying architecture
 */
class ToolManager : public QObject
{
    Q_OBJECT
public:
    explicit ToolManager(QObject *parent = nullptr);

    // ─── PDF ───
    void extractPdfText(const QString &filepath, std::function<void(const QString&)> callback);

    // ─── Image ───
    QImage loadImage(const QString &filepath);
    bool validateImage(const QImage &image);

    // ─── Terminal ───
    int executeCommand(const QString &command, const QString &workingDir = {});
    void cancelCommand(int id);

signals:
    void commandOutput(int id, const QString &output);
    void commandFinished(int id, int exitCode);
    void commandError(int id, const QString &error);

private:
    int m_nextCommandId = 1;
};

#endif
