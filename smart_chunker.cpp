#include "smart_chunker.h"
#include <QRegularExpression>
#include <QtMath>

SmartChunker::SmartChunker() {
    initSectionPriority();
}

void SmartChunker::initSectionPriority() {
    m_sectionPriority["Features"] = 100;
    m_sectionPriority["Overview"] = 95;
    m_sectionPriority["Pin"] = 90;
    m_sectionPriority["Memory"] = 85;
    m_sectionPriority["Register"] = 80;
    m_sectionPriority["Electrical"] = 70;
}

QString SmartChunker::normalize(const QString &text) {
    QString t = text;
    t.replace("\r\n", "\n");
    t.replace("\r", "\n");
    t.replace("\t", " ");
    t.replace(QRegularExpression("[ ]+"), " ");
    t.replace(QRegularExpression("\n{3,}"), "\n\n");
    return t.trimmed();
}

QStringList SmartChunker::splitSections(const QString &text) {
    QStringList sections = text.split(QRegularExpression("\n\\s*\n"), Qt::SkipEmptyParts);
    QStringList result;
    for (const QString &s : sections) {
        QString sec = s.trimmed();
        if (!sec.isEmpty())
            result.append(sec);
    }
    return result;
}

int SmartChunker::estimateTokens(const QString &text) const {
    return qMax(1, text.length() / 4);
}

QList<Chunk> SmartChunker::buildSlidingWindow(const QString &section, int chunkTokens, double overlap) {
    QList<Chunk> result;
    QStringList words = section.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (words.isEmpty())
        return result;

    int windowSize = qMax(100, chunkTokens);
    int step = qMax(50, int(windowSize * (1.0 - overlap)));

    for (int start = 0; start < words.size(); start += step) {
        int end = qMin(start + windowSize, words.size());
        if (start >= words.size()) break;
        QString chunkText;
        for (int i = start; i < end; ++i) {
            chunkText += words[i];
            if (i != end - 1)
                chunkText += " ";
        }

        Chunk c;
        c.text = chunkText;
        c.tokenCount = estimateTokens(chunkText);
        result.append(c);

        if (end == words.size())
            break;
    }

    return result;
}

QList<Chunk> SmartChunker::build(const QString &text, int chunkTokens, double overlap) {
    QList<Chunk> result;
    QString normalized = normalize(text);
    QStringList sections = splitSections(normalized);

    int index = 0;
    for (const QString &sec : sections) {
        QList<Chunk> chunks = buildSlidingWindow(sec, chunkTokens, overlap);
        for (Chunk &c : chunks) {
            c.chunkIndex = index++;
            c.pageNumber = detectPageNumber(c.text);
            c.section = detectSection(c.text);
            c.priority = m_sectionPriority.value(c.section, 10);
            c.isTable = c.text.contains(QRegularExpression("\\|.*\\|")) &&
                         c.text.count('\n') > 2;
            result.append(c);
        }
    }

    return result;
}

int SmartChunker::detectPageNumber(const QString &text) {
    QRegularExpression re(R"(Page\s+(\d+))", QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch match = re.match(text);
    if (match.hasMatch()) {
        return match.captured(1).toInt();
    }
    return 1;
}

QString SmartChunker::detectSection(const QString &text) {
    QString t = text.toLower();
    if (t.contains("features")) return "Features";
    if (t.contains("overview")) return "Overview";
    if (t.contains("pin configuration")) return "Pin";
    if (t.contains("memory organization")) return "Memory";
    if (t.contains("register summary")) return "Register";
    return "General";
}