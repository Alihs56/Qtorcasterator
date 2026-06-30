#ifndef PROJECTWORKSPACEMANAGER_H
#define PROJECTWORKSPACEMANAGER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <functional>
#include <QMutex>

class LanguageDetector;

class ProjectWorkspaceManager : public QObject
{
    Q_OBJECT
public:
    struct ProjectInfo {
        QString rootPath;
        QString projectName;
        QStringList detectedLanguages;
        QStringList sourceFiles;
        QStringList headerFiles;
        QStringList configurationFiles;
        QString buildSystem;
        QStringList dependencies;
        QMap<QString, QStringList> filesByLanguage;
    };

    explicit ProjectWorkspaceManager(LanguageDetector *detector, QObject *parent = nullptr);
    ~ProjectWorkspaceManager() override;

    bool openProject(const QString &path);
    void closeProject();

    ProjectInfo currentProject() const;
    QString projectRoot() const;
    bool isProjectOpen() const;

    QStringList detectLanguages(const QString &projectPath) const;
    QString detectBuildSystem(const QString &projectPath) const;
    QStringList findFiles(const QString &projectPath, const QStringList &filters) const;
    QStringList findSourceFiles(const QString &projectPath) const;
    QStringList findHeaderFiles(const QString &projectPath) const;

    void refreshProject();

signals:
    void projectOpened(const ProjectInfo &info);
    void projectClosed();
    void projectRefreshed(const ProjectInfo &info);
    void error(const QString &message);

private:
    void scanProject(const QString &path);
    void detectProjectMetadata(ProjectInfo &info) const;

    LanguageDetector *m_detector = nullptr;
    ProjectInfo m_currentProject;
    bool m_isOpen = false;
    mutable QMutex m_mutex;
};

#endif // PROJECTWORKSPACEMANAGER_H
