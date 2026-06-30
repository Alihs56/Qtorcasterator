#include "ContextCompressor.h"
#include <QRegularExpression>
#include <QSet>
#include <QDebug>
#include <algorithm>

ContextCompressor::ContextCompressor(QObject *parent)
    : QObject(parent)
{
}

// بازنویسی با استراتژی Head-Tail (حفظ ابتدا و انتهای متن)
QString ContextCompressor::compress(const QString &context, int maxTokens) {
    int currentTokens = estimateTokens(context);
    if (currentTokens <= maxTokens) return context;

    emit compressionStarted(context.length());

    QStringList lines = context.split('\n');
    int totalLines = lines.size();
    
    // هدف: حفظ ۲۰٪ اول (تعاریف) و ۴۰٪ آخر (سوال کاربر) و حذف تدریجی وسط
    int keepStart = totalLines * 0.2;
    int keepEnd = totalLines * 0.4;
    
    QStringList compressedLines;
    for (int i = 0; i < keepStart; ++i) {
        compressedLines.append(lines.at(i));
    }
    
    compressedLines.append("\n[... " + QString::number(totalLines - keepStart - keepEnd) + " lines removed for brevity ...]\n");
    
    for (int i = totalLines - keepEnd; i < totalLines; ++i) {
        compressedLines.append(lines.at(i));
    }

    QString result = compressedLines.join('\n');
    
    // اگه باز هم زیاد بود، به صورت بازگشتی از ته می‌بریم
    if (estimateTokens(result) > maxTokens) {
        return result.left(maxTokens * 4); // قطعیت نهایی
    }

    emit compressionFinished(context.length(), result.length());
    return result;
}

QString ContextCompressor::compressWithStrategy(const QString &context, int maxTokens, const QString &strategy)
{
    Q_UNUSED(strategy);
    return compress(context, maxTokens);
}

QString ContextCompressor::removeDuplicates(const QString &text)
{
    QStringList lines = text.split('\n');
    QSet<QString> seen;
    QStringList unique;

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || seen.contains(trimmed))
            continue;
        seen.insert(trimmed);
        unique.append(line);
    }
    return unique.join('\n');
}

QString ContextCompressor::removeUnnecessaryComments(const QString &code)
{
    QString result = code;
    QStringList lines = result.split('\n');
    QStringList filtered;

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.startsWith("//") && trimmed.length() < 20)
            continue;
        if (trimmed.startsWith("/*") && trimmed.endsWith("*/"))
            continue;
        filtered.append(line);
    }
    return filtered.join('\n');
}

QString ContextCompressor::removeIrrelevantChunks(const QString &context, const QString &query)
{
    Q_UNUSED(query);
    QString result = context;
    QSet<QString> keepKeywords = {
        "class", "struct", "enum", "void", "int main", "signals:", "public slots:",
        "Q_OBJECT", "Q_PROPERTY", "return", "if", "while", "for"
    };

    QStringList blocks = extractCodeBlocks(result);
    QStringList filtered;
    for (const QString &block : blocks) {
        bool relevant = false;
        for (const QString &kw : keepKeywords) {
            if (block.contains(kw, Qt::CaseInsensitive)) {
                relevant = true;
                break;
            }
        }
        if (relevant || block.length() < 50)
            filtered.append(block);
        else
            filtered.append("// [removed — low relevance chunk]");
    }
    return filtered.join("\n");
}

QString ContextCompressor::keepImportantDefinitions(const QString &context)
{
    return context;
}

QStringList ContextCompressor::extractCodeBlocks(const QString &text)
{
    QStringList blocks;
    QRegularExpression re(R"(```[\s\S]*?```)");
    int start = 0;
    while ((start = text.indexOf(re, start)) != -1) {
        blocks.append(text.mid(start, re.match(text, start).capturedLength()));
        start += re.match(text, start).capturedLength();
    }
    if (blocks.isEmpty())
        blocks.append(text);
    return blocks;
}

int ContextCompressor::estimateTokens(const QString &text)
{
    return text.length() / CHARS_PER_TOKEN;
}

double ContextCompressor::similarity(const QString &a, const QString &b) const
{
    if (a.isEmpty() || b.isEmpty()) return 0.0;
    if (a == b) return 1.0;

    QSet<QString> setA(a.toLower().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).begin(),
                       a.toLower().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).end());
    QSet<QString> setB(b.toLower().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).begin(),
                       b.toLower().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).end());

    if (setA.isEmpty() || setB.isEmpty()) return 0.0;

    int intersection = 0;
    for (const QString &s : setA) {
        if (setB.contains(s))
            ++intersection;
    }
    int unionSize = setA.size() + setB.size() - intersection;
    return unionSize > 0 ? (double)intersection / unionSize : 0.0;
}

QStringList ContextCompressor::findDependencies(const QString &code) const
{
    QStringList deps;
    QRegularExpression includeRe(R"(#include\s+[<"]([^>"]+)[>"])");
    int start = 0;
    QRegularExpressionMatch match;
    while ((start = code.indexOf(includeRe, start, &match)) != -1) {
        deps.append(match.captured(1));
        start += match.capturedLength();
    }
    return deps;
}

QString ContextCompressor::extractBlock(const QString &text, const QString &startMarker,
                                        const QString &endMarker) const
{
    int start = text.indexOf(startMarker);
    if (start < 0) return {};
    int end = text.indexOf(endMarker, start + startMarker.length());
    if (end < 0) return text.mid(start);
    return text.mid(start, end - start + endMarker.length());
}
