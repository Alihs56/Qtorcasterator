#ifndef RERANKER_H
#define RERANKER_H

#include <QObject>
#include <QString>
#include <QList>
#include <functional>
#include <QVector>

class Retriever;

class Reranker : public QObject
{
    Q_OBJECT
public:
    struct RankedItem {
        QString text;
        QString metadata;
        double originalScore = 0.0;
        double rerankScore = 0.0;
        int rank = 0;
    };

    explicit Reranker(QObject *parent = nullptr);
    ~Reranker() override = default;

    void rerank(const QString &query, const QList<QString> &candidates,
                std::function<void(const QList<RankedItem>&)> callback);

    QList<RankedItem> rerankSync(const QString &query, const QList<QString> &candidates);

    void setEmbedFunction(std::function<QVector<float>(const QString&)> fn);

signals:
    void rerankCompleted(const QList<RankedItem> &items);
    void rerankError(const QString &error);

private:
    double computeRelevance(const QString &query, const QString &candidate) const;
    double computeDiversity(const QString &a, const QString &b) const;

    std::function<QVector<float>(const QString&)> m_embedFn;
};

#endif // RERANKER_H
