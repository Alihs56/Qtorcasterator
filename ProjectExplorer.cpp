#include "ProjectExplorer.h"

ProjectExplorer::ProjectExplorer(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_filterInput = new QLineEdit(this);
    m_filterInput->setPlaceholderText("Filter files...");
    m_filterInput->setClearButtonEnabled(true);

    m_model = new QFileSystemModel(this);
    m_model->setRootPath(QDir::rootPath());
    m_model->setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);

    m_proxyModel = new QSortFilterProxyModel(this);
    m_proxyModel->setSourceModel(m_model);
    m_proxyModel->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxyModel->setRecursiveFilteringEnabled(true);

    m_treeView = new QTreeView(this);
    m_treeView->setModel(m_proxyModel);
    m_treeView->setRootIndex(m_proxyModel->mapFromSource(m_model->index(QDir::rootPath())));
    m_treeView->setAnimated(true);
    m_treeView->setIndentation(16);
    m_treeView->setHeaderHidden(true);
    m_treeView->setSortingEnabled(true);
    m_treeView->sortByColumn(0, Qt::AscendingOrder);
    m_treeView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_treeView->setContextMenuPolicy(Qt::CustomContextMenu);

    layout->addWidget(m_filterInput);
    layout->addWidget(m_treeView, 1);

    connect(m_treeView, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        if (!index.isValid()) return;
        QString filePath = m_model->filePath(m_proxyModel->mapToSource(index));
        if (!QFileInfo(filePath).isDir())
            emit fileSelected(filePath);
    });

    connect(m_treeView, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!index.isValid()) return;
        QString filePath = m_model->filePath(m_proxyModel->mapToSource(index));
        emit fileDoubleClicked(filePath);
    });

    connect(m_filterInput, &QLineEdit::textChanged, m_proxyModel, &QSortFilterProxyModel::setFilterWildcard);
}

void ProjectExplorer::setRootPath(const QString &path) {
    m_rootPath = path;
    if (QDir(path).exists()) {
        m_model->setRootPath(path);
        m_treeView->setRootIndex(m_proxyModel->mapFromSource(m_model->index(path)));
    }
}

QString ProjectExplorer::rootPath() const {
    return m_rootPath;
}

void ProjectExplorer::setTheme(bool dark) {
    QString bg = dark ? "#1a1a2e" : "#ffffff";
    QString fg = dark ? "#d0d0e4" : "#333333";
    QString alt = dark ? "#16162a" : "#f5f5f5";
    setStyleSheet(QString(
        "ProjectExplorer { background-color: %1; color: %2; }"
        "QTreeView { background-color: %1; color: %2; border: none; "
        "  alternate-background-color: %3; font-size: 12px; }"
        "QTreeView::item { padding: 2px 4px; }"
        "QTreeView::item:selected { background-color: #3a3a5a; color: #e0e0f0; }"
        "QLineEdit { background-color: %3; color: %2; border: 1px solid #2a2a4a; "
        "  border-radius: 3px; padding: 3px 6px; }"
    ).arg(bg, fg, alt));
}
