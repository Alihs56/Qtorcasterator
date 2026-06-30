#ifndef CONTEXTCOMPRESSOR_H
#define CONTEXTCOMPRESSOR_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <functional>

class ContextCompressor : public QObject
{
    Q_OBJECT
public:
    explicit ContextCompressor(QObject *parent = nullptr);
    ~ContextCompressor() override = default;

    QString compress(const QString &context, int maxTokens = 4000);
    QString compressWithStrategy(const QString &context, int maxTokens, const QString &strategy);

    QString removeDuplicates(const QString &text);
    QString removeUnnecessaryComments(const QString &code);
    QString removeIrrelevantChunks(const QString &context, const QString &query);
    QString keepImportantDefinitions(const QString &context);

    static QStringList extractCodeBlocks(const QString &text);
    static int estimateTokens(const QString &text);

signals:
    void compressionStarted(int originalSize);
    void compressionFinished(int originalSize, int compressedSize);
    void compressionError(const QString &error);

private:
    double similarity(const QString &a, const QString &b) const;
    QStringList findDependencies(const QString &code) const;
    QString extractBlock(const QString &text, const QString &startMarker, const QString &endMarker) const;

    static constexpr int CHARS_PER_TOKEN = 4;
};

#endif // CONTEXTCOMPRESSOR_H
