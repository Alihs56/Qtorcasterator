#include "ProjectWorkspaceManager.h"
#include "LanguageDetector.h"
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QDebug>

ProjectWorkspaceManager::ProjectWorkspaceManager(LanguageDetector *detector, QObject *parent)
    : QObject(parent), m_detector(detector)
{
}

ProjectWorkspaceManager::~ProjectWorkspaceManager() = default;

bool ProjectWorkspaceManager::openProject(const QString &path)
{
    QMutexLocker locker(&m_mutex);
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isDir())
        return false;

    m_currentProject.rootPath = fi.canonicalFilePath();
    m_currentProject.projectName = fi.fileName();
    scanProject(m_currentProject.rootPath);
    detectProjectMetadata(m_currentProject);
    m_isOpen = true;

    emit projectOpened(m_currentProject);
    return true;
}

void ProjectWorkspaceManager::closeProject()
{
    QMutexLocker locker(&m_mutex);
    m_currentProject = ProjectInfo();
    m_isOpen = false;
    emit projectClosed();
}

ProjectWorkspaceManager::ProjectInfo ProjectWorkspaceManager::currentProject() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentProject;
}

QString ProjectWorkspaceManager::projectRoot() const
{
    QMutexLocker locker(&m_mutex);
    return m_currentProject.rootPath;
}

bool ProjectWorkspaceManager::isProjectOpen() const
{
    QMutexLocker locker(&m_mutex);
    return m_isOpen;
}

QStringList ProjectWorkspaceManager::detectLanguages(const QString &projectPath) const
{
    QSet<QString> langs;
    QStringList filters = {"*.cpp", "*.h", "*.hpp", "*.py", "*.java", "*.js", "*.ts", "*.rs", "*.go", "*.cs", "*.c", "*.cc", "*.cxx"};
    QDirIterator it(projectPath, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo fi(path);
        QString ext = fi.suffix().toLower();
        LanguageDetector::Language lang = m_detector->detect(path);
        if (lang != LanguageDetector::Unknown)
            langs.insert(m_detector->languageName(lang));
    }
    return langs.values();
}

QString ProjectWorkspaceManager::detectBuildSystem(const QString &projectPath) const
{
    QDir dir(projectPath);
    QStringList buildFiles = {"CMakeLists.txt", "Makefile", "*.pro", "pom.xml", "build.gradle",
                              "package.json", "Cargo.toml", "go.mod", "*.csproj", "setup.py", "pyproject.toml"};
    for (const QString &pattern : buildFiles) {
        if (pattern.startsWith("*.")) {
            QStringList files = dir.entryList(QStringList(pattern), QDir::Files);
            if (!files.isEmpty())
                return pattern;
        } else {
            if (dir.exists(pattern))
                return pattern;
        }
    }
    return "unknown";
}

QStringList ProjectWorkspaceManager::findFiles(const QString &projectPath, const QStringList &filters) const
{
    QStringList files;
    QDirIterator it(projectPath, filters, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(it.next());
    return files;
}

QStringList ProjectWorkspaceManager::findSourceFiles(const QString &projectPath) const
{
    return findFiles(projectPath, {"*.cpp", "*.c", "*.cc", "*.cxx", "*.py", "*.java", "*.js", "*.ts", "*.rs", "*.go", "*.cs"});
}

QStringList ProjectWorkspaceManager::findHeaderFiles(const QString &projectPath) const
{
    return findFiles(projectPath, {"*.h", "*.hpp", "*.hxx"});
}

void ProjectWorkspaceManager::refreshProject()
{
    QMutexLocker locker(&m_mutex);
    if (!m_isOpen)
        return;
    scanProject(m_currentProject.rootPath);
    detectProjectMetadata(m_currentProject);
    emit projectRefreshed(m_currentProject);
}

void ProjectWorkspaceManager::scanProject(const QString &path)
{
    ProjectInfo &info = m_currentProject;
    info.sourceFiles = findSourceFiles(path);
    info.headerFiles = findHeaderFiles(path);
}

void ProjectWorkspaceManager::detectProjectMetadata(ProjectInfo &info) const
{
    info.detectedLanguages = detectLanguages(info.rootPath);
    info.buildSystem = detectBuildSystem(info.rootPath);

    info.filesByLanguage.clear();
    for (const QString &file : info.sourceFiles) {
        LanguageDetector::Language lang = m_detector->detect(file);
        QString langName = m_detector->languageName(lang);
        info.filesByLanguage[langName].append(file);
    }
}
