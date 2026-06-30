#ifndef CODEPARSER_H
#define CODEPARSER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QMap>
#include <QVariant>

#include "LanguageDetector.h"

struct SymbolInfo {
    enum Type {
        Unknown = 0,
        Class,
        Struct,
        Enum,
        Function,
        Method,
        Namespace,
        Include,
        Inheritance,
        Comment,
        Documentation
    };
    Type type = Unknown;
    QString name;
    QString qualifiedName;
    QString namespace_;
    QString parentClass;
    QString signature;
    QString returnType;
    QStringList parameters;
    int startLine = 0;
    int endLine = 0;
    QString documentation;
    QStringList baseClasses;
    QString filePath;
    QString language;
};

class CodeParser : public QObject
{
    Q_OBJECT
public:
    explicit CodeParser(LanguageDetector *detector, QObject *parent = nullptr);
    ~CodeParser() override = default;

    QList<SymbolInfo> parse(const QString &filePath);
    QList<SymbolInfo> parseContent(const QString &content, const QString &filePath, LanguageDetector::Language lang);
    QString astSummary(const QString &filePath);

    static QString symbolTypeName(SymbolInfo::Type type);
    static SymbolInfo::Type stringToSymbolType(const QString &typeStr);

signals:
    void parseCompleted(const QString &filePath, const QList<SymbolInfo> &symbols);
    void parseError(const QString &filePath, const QString &error);

private:
    QList<SymbolInfo> parseCpp(const QString &content, const QString &filePath);
    QList<SymbolInfo> parsePython(const QString &content, const QString &filePath);
    QList<SymbolInfo> parseJavaScript(const QString &content, const QString &filePath);
    QList<SymbolInfo> parseTypeScript(const QString &content, const QString &filePath);
    QList<SymbolInfo> parseJava(const QString &content, const QString &filePath);
    QList<SymbolInfo> parseCSharp(const QString &content, const QString &filePath);
    QList<SymbolInfo> parseRust(const QString &content, const QString &filePath);
    QList<SymbolInfo> parseGo(const QString &content, const QString &filePath);

    QStringList extractComments(const QString &content, LanguageDetector::Language lang) const;
    QStringList extractDocumentation(const QString &content, LanguageDetector::Language lang) const;
    QList<QPair<int, int>> findBlocks(const QString &content, const QChar &open, const QChar &close) const;

    LanguageDetector *m_detector = nullptr;
    int findBlockEnd(const QStringList &lines, int startLine);
    int findPythonBlockEnd(const QStringList &lines, int startLine);

};

#endif // CODEPARSER_H
