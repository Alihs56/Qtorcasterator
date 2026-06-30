#include "Reranker.h"
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QDebug>
#include <algorithm>

Reranker::Reranker(QObject *parent)
    : QObject(parent)
{
}

void Reranker::rerank(const QString &query, const QList<QString> &candidates,
                      std::function<void(const QList<RankedItem>&)> callback)
{
    QList<RankedItem> results = rerankSync(query, candidates);
    callback(results);
}

QList<Reranker::RankedItem> Reranker::rerankSync(const QString &query, const QList<QString> &candidates)
{
    QList<RankedItem> items;
    for (int i = 0; i < candidates.size(); ++i) {
        RankedItem item;
        item.text = candidates.at(i);
        item.metadata = item.text;
        item.originalScore = computeRelevance(query, item.text);
        items.append(item);
    }

    std::sort(items.begin(), items.end(), [](const RankedItem &a, const RankedItem &b) {
        return a.originalScore > b.originalScore;
    });

    QSet<QString> seen;
    for (RankedItem &item : items) {
        if (!seen.isEmpty()) {
            double diversityPenalty = 0.0;
            for (const RankedItem &other : items) {
                if (&other != &item && seen.contains(other.text.left(50))) {
                    diversityPenalty += 0.1;
                }
            }
            item.rerankScore = item.originalScore - diversityPenalty;
        } else {
            item.rerankScore = item.originalScore;
        }
        seen.insert(item.text.left(80));
    }

    std::sort(items.begin(), items.end(), [](const RankedItem &a, const RankedItem &b) {
        return a.rerankScore > b.rerankScore;
    });

    for (int i = 0; i < items.size(); ++i)
        items[i].rank = i + 1;

    return items;
}

void Reranker::setEmbedFunction(std::function<QVector<float>(const QString&)> fn)
{
    m_embedFn = fn;
}

double Reranker::computeRelevance(const QString &query, const QString &candidate) const
{
    QStringList queryTerms = query.toLower().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    QStringList candidateTerms = candidate.toLower().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);

    QSet<QString> querySet(queryTerms.begin(), queryTerms.end());
    QSet<QString> candidateSet(candidateTerms.begin(), candidateTerms.end());

    int intersection = 0;
    for (const QString &term : querySet) {
        if (candidateSet.contains(term))
            ++intersection;
    }

    int querySize = qMax(querySet.size(), 1);
    double keywordScore = (double)intersection / querySize;

    double lengthBonus = 0.0;
    int idealMinLen = 100;
    int idealMaxLen = 2000;
    int len = candidate.length();
    if (len >= idealMinLen && len <= idealMaxLen)
        lengthBonus = 0.1;
    else if (len < idealMinLen)
        lengthBonus = (double)len / idealMinLen * 0.05;

    return qMin(1.0, keywordScore + lengthBonus);
}

double Reranker::computeDiversity(const QString &a, const QString &b) const
{
    QSet<QString> setA = QSet<QString>::fromList(a.toLower().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts));
    QSet<QString> setB = QSet<QString>::fromList(b.toLower().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts));

    int intersection = 0;
    for (const QString &s : setA) {
        if (setB.contains(s))
            ++intersection;
    }

    int unionSize = qMax(setA.size() + setB.size() - intersection, 1);
    return 1.0 - (double)intersection / unionSize;
}
