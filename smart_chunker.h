#ifndef SMART_CHUNKER_H
#define SMART_CHUNKER_H

#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>

struct Chunk {
    QString text;
    int chunkIndex = 0;
    int tokenCount = 0;
    int pageNumber = 0;
    QString section;
    int priority = 10;
    bool isTable = false;
};

class SmartChunker {
public:
    SmartChunker();

    QList<Chunk> build(const QString &text, int chunkTokens, double overlap);

private:
    QString normalize(const QString &text);
    QStringList splitSections(const QString &text);
    QList<Chunk> buildSlidingWindow(const QString &section, int chunkTokens, double overlap);
    int estimateTokens(const QString &text) const;
    void initSectionPriority();
    int detectPageNumber(const QString &text);
    QString detectSection(const QString &text);

    QMap<QString, int> m_sectionPriority;
};

#endif