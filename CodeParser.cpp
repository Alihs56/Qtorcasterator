#include "CodeParser.h"
#include "LanguageDetector.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDebug>

CodeParser::CodeParser(LanguageDetector *detector, QObject *parent)
    : QObject(parent), m_detector(detector)
{
}

QList<SymbolInfo> CodeParser::parse(const QString &filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QTextStream ts(&file);
    QString content = ts.readAll();
    file.close();

    LanguageDetector::Language lang = m_detector->detect(filePath);
    return parseContent(content, filePath, lang);
}

QList<SymbolInfo> CodeParser::parseContent(const QString &content, const QString &filePath, LanguageDetector::Language lang)
{
    switch (lang) {
    case LanguageDetector::Cpp:      return parseCpp(content, filePath);
    case LanguageDetector::Python:   return parsePython(content, filePath);
    case LanguageDetector::JavaScript: return parseJavaScript(content, filePath);
    case LanguageDetector::TypeScript: return parseTypeScript(content, filePath);
    case LanguageDetector::Java:     return parseJava(content, filePath);
    case LanguageDetector::CSharp:   return parseCSharp(content, filePath);
    case LanguageDetector::Rust:     return parseRust(content, filePath);
    case LanguageDetector::Go:       return parseGo(content, filePath);
    default: return {};
    }
}

QString CodeParser::astSummary(const QString &filePath)
{
    QList<SymbolInfo> symbols = parse(filePath);
    QString summary = QString("AST Summary for %1\n").arg(filePath);

    for (const SymbolInfo &sym : symbols) {
        summary += QString("L%1-%2: [%3] %4\n")
                       .arg(sym.startLine)
                       .arg(sym.endLine)
                       .arg(symbolTypeName(sym.type))
                       .arg(sym.qualifiedName.isEmpty() ? sym.name : sym.qualifiedName);
    }
    return summary;
}

QString CodeParser::symbolTypeName(SymbolInfo::Type type)
{
    switch (type) {
        case SymbolInfo::Class: return "class";
        case SymbolInfo::Struct: return "struct";
        case SymbolInfo::Enum: return "enum";
        case SymbolInfo::Function: return "function";
        case SymbolInfo::Method: return "method";
        case SymbolInfo::Namespace: return "namespace";
        case SymbolInfo::Include: return "include";
        case SymbolInfo::Inheritance: return "inheritance";
        case SymbolInfo::Comment: return "comment";
        case SymbolInfo::Documentation: return "doc";
        default: return "unknown";
    }
}

SymbolInfo::Type CodeParser::stringToSymbolType(const QString &typeStr)
{
    if (typeStr == "class") return SymbolInfo::Class;
    if (typeStr == "struct") return SymbolInfo::Struct;
    if (typeStr == "enum") return SymbolInfo::Enum;
    if (typeStr == "function") return SymbolInfo::Function;
    if (typeStr == "method") return SymbolInfo::Method;
    if (typeStr == "namespace") return SymbolInfo::Namespace;
    if (typeStr == "include") return SymbolInfo::Include;
    if (typeStr == "inheritance") return SymbolInfo::Inheritance;
    if (typeStr == "comment") return SymbolInfo::Comment;
    if (typeStr == "doc") return SymbolInfo::Documentation;
    return SymbolInfo::Unknown;
}

QList<SymbolInfo> CodeParser::parseCpp(const QString &content, const QString &filePath)
{
    QList<SymbolInfo> symbols;
    QStringList lines = content.split('\n');

    QRegularExpression classRe(R"(\b(class|struct)\s+([A-Za-z_][\w:]*))");
    QRegularExpression funcRe(R"((?:^|\n)\s*(?:inline\s+|static\s+|virtual\s+|const\s+|explicit\s+)*((?:[\w:~]+\s+)+)(\w+)\s*\(([^)]*)\)\s*(?:const)?\s*(?:->\s*([\w:<\s&*]+))?\s*\{?))");
    QRegularExpression methodRe(R"((\w+)\s*::\s*~?\s*(\w+)\s*\(([^)]*)\)\s*(?:const)?)");
    QRegularExpression nsRe(R"(\bnamespace\s+([\w:]+)\s*\{)");
    QRegularExpression includeRe(R"(#include\s+[<"]([^>"]+)[>"])");
    QRegularExpression docRe(R"(/\*\*[\s\S]*?\*/)");
    QRegularExpression commentRe(R"(//\s*.*$|/\*[\s\S]*?\*/)");

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        int lineNum = i + 1;

        auto mc = classRe.match(line);
        if (mc.hasMatch()) {
            SymbolInfo sym;
            sym.type = (mc.captured(1) == "class") ? SymbolInfo::Class : SymbolInfo::Struct;
            sym.name = mc.captured(2).trimmed();
            sym.qualifiedName = sym.name;
            sym.startLine = lineNum;
            sym.endLine = findBlockEnd(lines, lineNum); 
            sym.filePath = filePath;
            sym.language = "cpp";
            symbols.append(sym);
        }

        auto nm = nsRe.match(line);
        if (nm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Namespace;
            sym.name = nm.captured(1).trimmed();
            sym.qualifiedName = sym.name;
            sym.startLine = lineNum;
            sym.endLine = findBlockEnd(lines, lineNum); 
            sym.filePath = filePath;
            sym.language = "cpp";
            symbols.append(sym);
        }

        auto im = includeRe.match(line);
        if (im.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Include;
            sym.name = im.captured(1).trimmed();
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "cpp";
            symbols.append(sym);
        }
    }
    return symbols;
}

// اضافه کردن این تابع به CodeParser.cpp برای پشتیبانی از پایتون
int CodeParser::findPythonBlockEnd(const QStringList &lines, int startLine) {
    if (startLine <= 0 || startLine > lines.size()) return startLine;

    // ۱. پیدا کردن میزان فاصله خط شروع (مثلاً ۴ تا Space)
    auto getIndent = [](const QString &line) {
        int indent = 0;
        for (QChar c : line) {
            if (c == ' ') indent++;
            else if (c == '\t') indent += 4;
            else break;
        }
        return indent;
    };

    int startIndent = getIndent(lines[startLine - 1]);
    int lastValidLine = startLine;

    // ۲. حرکت به سمت پایین تا رسیدن به خطی با ایندنت کمتر یا مساوی
    for (int i = startLine; i < lines.size(); ++i) {
        QString line = lines[i];
        if (line.trimmed().isEmpty()) continue; // نادیده گرفتن خطوط خالی

        int currentIndent = getIndent(line);
        
        // اگر به خطی رسیدیم که فاصله اش کمتر یا مساوی خط شروع بود، بلاک تمام شده
        if (currentIndent <= startIndent) {
            break;
        }
        lastValidLine = i + 1;
    }
    return lastValidLine;
}

QList<SymbolInfo> CodeParser::parsePython(const QString &content, const QString &filePath)
{
    QList<SymbolInfo> symbols;
    QStringList lines = content.split('\n');

    QRegularExpression classRe(R"(\bclass\s+([A-Za-z_][\w]*)(?:\s*\(([^)]*)\))?:)");
    QRegularExpression funcRe(R"(^\s*def\s+([A-Za-z_][\w]*)\s*\(([^)]*)\)\s*(?:->\s*([\w\[\],\s]+))?\s*:)");
    QRegularExpression importRe(R"((?:import|from)\s+([\w.]+))");

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        int lineNum = i + 1;

        auto cm = classRe.match(line);
        if (cm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Class;
            sym.name = cm.captured(1).trimmed();
            sym.qualifiedName = sym.name;
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "python";
            symbols.append(sym);
        }

        auto fm = funcRe.match(line);
        if (fm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Function;
            sym.name = fm.captured(1).trimmed();
            sym.signature = line.trimmed();
            sym.parameters = fm.captured(2).split(',', Qt::SkipEmptyParts);
            if (fm.capturedLength() > 3)
                sym.returnType = fm.captured(3).trimmed();
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "python";
            symbols.append(sym);
        }
    }
    return symbols;
}

QList<SymbolInfo> CodeParser::parseJavaScript(const QString &content, const QString &filePath)
{
    QList<SymbolInfo> symbols;
    QStringList lines = content.split('\n');

    QRegularExpression classRe(R"(\bclass\s+([A-Za-z_][\w]*))");
    QRegularExpression funcRe(R"((?:function\s+(\w+)|const\s+(\w+)\s*=\s*(?:async\s*)?\()\s*\(([^)]*)\))");
    QRegularExpression methodRe(R"(\b(\w+)\s*\(([^)]*)\)\s*\{)");

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        int lineNum = i + 1;

        auto cm = classRe.match(line);
        if (cm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Class;
            sym.name = cm.captured(1).trimmed();
            sym.qualifiedName = sym.name;
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "javascript";
            symbols.append(sym);
        }

        auto fm = funcRe.match(line);
        if (fm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Function;
            sym.name = fm.captured(1).isEmpty() ? fm.captured(2) : fm.captured(1);
            sym.parameters = fm.captured(3).split(',', Qt::SkipEmptyParts);
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "javascript";
            symbols.append(sym);
        }
    }
    return symbols;
}

QList<SymbolInfo> CodeParser::parseTypeScript(const QString &content, const QString &filePath)
{
    return parseJavaScript(content, filePath);
}

QList<SymbolInfo> CodeParser::parseJava(const QString &content, const QString &filePath)
{
    QList<SymbolInfo> symbols;
    QStringList lines = content.split('\n');

    QRegularExpression classRe(R"(\b(?:public|private|protected)?\s*(?:final|abstract|sealed)?\s*class\s+([A-Za-z_][\w]*))");
    QRegularExpression funcRe(R"((?:public|private|protected)?\s*(?:static)?\s*(?:final)?\s*([\w<>[\],\s]+)\s+(\w+)\s*\(([^)]*)\)\s*(?:throws\s+[\w,\s]+)?\s*\{)");

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        int lineNum = i + 1;

        auto cm = classRe.match(line);
        if (cm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Class;
            sym.name = cm.captured(1).trimmed();
            sym.qualifiedName = sym.name;
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "java";
            symbols.append(sym);
        }

        auto fm = funcRe.match(line);
        if (fm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Method;
            sym.returnType = fm.captured(1).simplified();
            sym.name = fm.captured(2).trimmed();
            sym.parameters = fm.captured(3).split(',', Qt::SkipEmptyParts);
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "java";
            symbols.append(sym);
        }
    }
    return symbols;
}

QList<SymbolInfo> CodeParser::parseCSharp(const QString &content, const QString &filePath)
{
    return parseJava(content, filePath);
}

QList<SymbolInfo> CodeParser::parseRust(const QString &content, const QString &filePath)
{
    QList<SymbolInfo> symbols;
    QStringList lines = content.split('\n');

    QRegularExpression structRe(R"(\bstruct\s+([A-Za-z_][\w]*))");
    QRegularExpression fnRe(R"(\bfn\s+([A-Za-z_][\w]*)\s*\(([^)]*)\)\s*(?:->\s*([\w<>\[\],\s&]+))?\s*\{)");
    QRegularExpression enumRe(R"(\benum\s+([A-Za-z_][\w]*))");
    QRegularExpression modRe(R"(\bmod\s+([A-Za-z_][\w]*))");

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        int lineNum = i + 1;

        auto sm = structRe.match(line);
        if (sm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Struct;
            sym.name = sm.captured(1).trimmed();
            sym.qualifiedName = sym.name;
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "rust";
            symbols.append(sym);
        }

        auto em = enumRe.match(line);
        if (em.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Enum;
            sym.name = em.captured(1).trimmed();
            sym.qualifiedName = sym.name;
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "rust";
            symbols.append(sym);
        }

        auto mm = modRe.match(line);
        if (mm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Namespace;
            sym.name = mm.captured(1).trimmed();
            sym.qualifiedName = sym.name;
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "rust";
            symbols.append(sym);
        }

        auto fm = fnRe.match(line);
        if (fm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Function;
            sym.name = fm.captured(1).trimmed();
            sym.parameters = fm.captured(2).split(',', Qt::SkipEmptyParts);
            if (fm.capturedLength() > 3)
                sym.returnType = fm.captured(3).trimmed();
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "rust";
            symbols.append(sym);
        }
    }
    return symbols;
}

QList<SymbolInfo> CodeParser::parseGo(const QString &content, const QString &filePath)
{
    QList<SymbolInfo> symbols;
    QStringList lines = content.split('\n');

    QRegularExpression funcRe(R"(\bfunc\s+(?:\([^)]+\)\s+)?(\w+)\s*\(([^)]*)\)(?:\s*\([^)]*\))?(?:\s*[\w\[\],\s]+)?\s*\{)");
    QRegularExpression typeRe(R"(\btype\s+([A-Za-z_][\w]*)\s+(?:struct|interface))");
    QRegularExpression pkRe(R"(\bpackage\s+([\w]+))");

    for (int i = 0; i < lines.size(); ++i) {
        const QString &line = lines.at(i);
        int lineNum = i + 1;

        auto pm = pkRe.match(line);
        if (pm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Namespace;
            sym.name = pm.captured(1).trimmed();
            sym.qualifiedName = sym.name;
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "go";
            symbols.append(sym);
        }

        auto tm = typeRe.match(line);
        if (tm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Struct;
            sym.name = tm.captured(1).trimmed();
            sym.qualifiedName = sym.name;
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "go";
            symbols.append(sym);
        }

        auto fm = funcRe.match(line);
        if (fm.hasMatch()) {
            SymbolInfo sym;
            sym.type = SymbolInfo::Function;
            sym.name = fm.captured(1).trimmed();
            sym.parameters = fm.captured(2).split(',', Qt::SkipEmptyParts);
            sym.startLine = lineNum;
            sym.endLine = lineNum;
            sym.filePath = filePath;
            sym.language = "go";
            symbols.append(sym);
        }
    }
    return symbols;
}

QStringList CodeParser::extractComments(const QString &content, LanguageDetector::Language lang) const
{
    Q_UNUSED(content);
    Q_UNUSED(lang);
    return {};
}

QStringList CodeParser::extractDocumentation(const QString &content, LanguageDetector::Language lang) const
{
    Q_UNUSED(content);
    Q_UNUSED(lang);
    return {};
}

QList<QPair<int, int>> CodeParser::findBlocks(const QString &content, const QChar &open, const QChar &close) const
{
    QList<QPair<int, int>> blocks;
    int depth = 0;
    int start = -1;

    for (int i = 0; i < content.length(); ++i) {
        if (content.at(i) == open) {
            if (depth == 0) start = i;
            ++depth;
        } else if (content.at(i) == close) {
            --depth;
            if (depth == 0 && start >= 0)
                blocks.append(qMakePair(start, i));
        }
    }
    return blocks;
}

int CodeParser::findBlockEnd(const QStringList &lines, int startLine) {
    int bracketCount = 0;
    bool foundOpen = false;
    bool inString = false;

    // شروع جستجو از خطی که تابع/کلاس پیدا شده
    for (int i = startLine - 1; i < lines.size(); ++i) {
        QString line = lines[i];
        
        // نادیده گرفتن محتویات داخل کوتیشن برای اشتباه نشدن شمارش
        for (int c = 0; c < line.length(); ++c) {
            if (line[c] == '"' && (c == 0 || line[c-1] != '\\')) inString = !inString;
            if (inString) continue;

            if (line[c] == '{') {
                bracketCount++;
                foundOpen = true;
            } else if (line[c] == '}') {
                bracketCount--;
            }
        }

        // اگه براکت باز شده بود و حالا شمارش به صفر رسید، یعنی پایان بلاک
        if (foundOpen && bracketCount <= 0) {
            return i + 1; // شماره خط پایان
        }
    }
    return startLine; // اگه پایان پیدا نشد (خطای سینتکس در فایل اصلی)
}
