// ============================================================
// فایل: agents/build_pipeline.h
// ============================================================
#ifndef BUILD_PIPELINE_H
#define BUILD_PIPELINE_H

#include <QObject>
#include <QString>
#include <QStringList>

class TerminalExecutor;
class ApiClient;

class BuildPipeline : public QObject {
    Q_OBJECT
public:
    struct BuildResult {
        bool success = false;
        int exitCode = -1;
        QString output;
        QStringList errors;
        QStringList warnings;
        QString suggestions;  // از نسخه قدیمی
    };

    explicit BuildPipeline(TerminalExecutor *executor, ApiClient *api,
                           QObject *parent = nullptr);

    void setBuildDir(const QString &dir);
    void setBuildCommand(const QString &cmd, const QStringList &args = {});

    int build();
    int buildWithFix();
    bool isBuilding() const;

signals:
    void buildStarted(int id);
    void buildProgress(int id, const QString &output);
    void buildDone(int id, const BuildResult &result);
    void buildError(int id, const QString &error);  // از نسخه قدیمی
    void fixStarted(int id);
    void fixDone(int id, bool success, const QString &suggestion);

private:
    void parseErrors(const QString &output, QStringList &errors,
                     QStringList &warnings) const;
    QString generateFixPrompt(const BuildResult &result) const;

    TerminalExecutor *m_executor;
    ApiClient *m_api;
    QString m_buildDir;
    QString m_buildCmd = "make";
    QStringList m_buildArgs;
    bool m_building = false;
};

#endif