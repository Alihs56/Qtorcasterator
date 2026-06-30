#ifndef DEPENDENCYGRAPH_H
#define DEPENDENCYGRAPH_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QSet>
#include <QSqlDatabase>
#include <QMutex>
#include <functional>

class SymbolDatabase;
class CodeParser;

class DependencyGraph : public QObject
{
    Q_OBJECT
public:
    struct Dependency {
        QString from;
        QString to;
        QString type;
        QString sourceFile;
        int line = 0;
    };

    explicit DependencyGraph(const QString &dbPath = QString(), QObject *parent = nullptr);
    ~DependencyGraph() override;

    bool initialize();
    bool isReady() const;

    void addDependency(const Dependency &dep);
    void addFileDependency(const QString &from, const QString &to);
    void addInheritance(const QString &derived, const QString &base);
    void addQtConnection(const QString &sender, const QString &signal, const QString &receiver, const QString &slot);
    void addTemplate(const QString &templateName, const QString &instantiated);

    QList<Dependency> getDependencies(const QString &symbol) const;
    QList<Dependency> getDependents(const QString &symbol) const;
    QList<QString> getTransitiveDependencies(const QString &symbol, int maxDepth = 5) const;
    QList<QString> getReverseTransitiveDependencies(const QString &symbol, int maxDepth = 5) const;
    QStringList getAffectedFiles(const QString &symbol) const;
    QStringList suggestIncludeOrder(const QStringList &files) const;

    void analyzeProject(const QString &projectDir);
    void clear();

signals:
    void dependencyFound(const Dependency &dep);
    void graphError(const QString &error);

private:
    bool createTables();
    bool ensureConnection();
    void scanForIncludes(const QString &filePath, const QStringList &sourceFiles);

    mutable QMutex m_mutex;
    QMap<QString, QList<Dependency>> m_dependencies;
    QMap<QString, QList<Dependency>> m_reverseDependencies;
    QSqlDatabase m_db;
    QString m_dbPath;
    bool m_ready = false;
};

#endif // DEPENDENCYGRAPH_H
