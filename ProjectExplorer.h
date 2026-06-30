#ifndef PROJECTEXPLORER_H
#define PROJECTEXPLORER_H

#include <QWidget>
#include <QTreeView>
#include <QFileSystemModel>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QSortFilterProxyModel>
#include <QDir>

class ProjectExplorer : public QWidget {
    Q_OBJECT
public:
    explicit ProjectExplorer(QWidget *parent = nullptr);

    void setRootPath(const QString &path);
    void setRoot(const QString &path) { setRootPath(path); }
    void setTheme(bool dark);
    QString rootPath() const;
    QTreeView *treeView() const { return m_treeView; }

signals:
    void fileSelected(const QString &filePath);
    void fileDoubleClicked(const QString &filePath);

private:
    QTreeView *m_treeView = nullptr;
    QFileSystemModel *m_model = nullptr;
    QSortFilterProxyModel *m_proxyModel = nullptr;
    QLineEdit *m_filterInput = nullptr;
    QString m_rootPath;
};

#endif
